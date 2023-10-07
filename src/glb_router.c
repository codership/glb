/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * NOTE: connection count and usage make sense only for standalone balancer
 *       so all operations on them are #ifdef GLBD ... #endif
 *
 * $Id: glb_router.c 156 2013-08-23 08:24:56Z vlad $
 */

#include "glb_router.h"
#include "glb_log.h"
#include "glb_cmd.h"
#include "glb_misc.h"
#include "glb_socket.h"

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h> // for close()
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <float.h> // for DBL_EPSILON

static double const GLB_DBL_EPSILON = (DBL_EPSILON * 2.0);

#ifdef GLBD
#  include <stdio.h>
#else /* GLBD */
#  include <dlfcn.h>
static int (*__glb_real_connect) (int                    sockfd,
                                  const struct sockaddr* addr,
                                  socklen_t              addrlen) = NULL;
#endif /* GLBD */

/*! the decision context */
typedef struct router_ctx
{
    double min_weight;
    long   retry;
    time_t now;
} router_ctx_t;

typedef struct router_dst
{
    glb_dst_t  dst;
    glb_backend_thread_ctx_t* probe_ctx;
#ifdef GLBD
    double     usage;   // usage measure: weight/(conns + 1) - bigger wins
#endif
    double     map;     // is used to break (0.0, 1.0) proportionally to weight
    glb_time_t checked; // last time this destination was checked
    time_t     failed;  // last time connection to this destination failed
#ifdef GLBD
    int        conns;   // how many connections use this destination
#endif
} router_dst_t;

struct glb_router
{
    const volatile glb_cnf_t* cnf;
    glb_sockaddr_t  sock_out; // outgoing socket address
    pthread_mutex_t lock;
    router_ctx_t    ctx;
    long            busy_count;
    long            wait_count;
    pthread_cond_t  free;
#ifdef GLBD
    int             conns;
#endif
    unsigned int    seed;     // seed for rng
    int             rrb_next; // round-robin cursor
    int             n_dst;
    time_t          map_failed; // last time the map was redone after failed dst
    time_t          top_failed; // last time top dst was redone after failed dst
    router_dst_t*   top_dst;
    router_dst_t*   dst;
};

static const double router_div_prot = 1.0e-09; // protection against div by 0

// seconds (should be >= 1 due to time_t precision)
static inline long
router_retry_interval (const glb_router_t* const router)
{
    return (glb_time_approx_seconds(router->cnf->interval) + 1);
}

static bool
router_dst_probe (router_dst_t* const d, glb_time_t now)
{
    glb_wdog_check_t p;
    struct timespec until = glb_time_to_timespec(now);
    until.tv_sec += 1; // 1 second timeout

    glb_backend_probe (d->probe_ctx, &p, &until);

    if (GLB_DST_READY == p.state)
    {
        d->checked = p.timestamp;
    }
    else
    {
        d->failed = time (NULL);
    }

    return (GLB_DST_READY == p.state);
}

static inline bool
router_dst_is_good_base (const router_dst_t* const d,
                         time_t              const now,
                         long                const retry)
{
    return (difftime (now, d->failed) > retry);
}


static inline bool
router_dst_is_good (const router_dst_t* const d,
                    double              const min_weight,
                    time_t              const now,
                    long                const retry)
{
    return (d->dst.weight >= min_weight &&
            router_dst_is_good_base (d, now, retry));
}

static inline bool
router_top_dst_is_good (const glb_router_t* const router)
{
    const router_dst_t* const d = router->top_dst;
    return (d && d->dst.weight >= GLB_DBL_EPSILON &&
            router_dst_is_good_base (d, router->ctx.now, router->ctx.retry));
}

static inline double
router_min_weight (const glb_router_t* const router)
{
    return (router_top_dst_is_good (router) ?
            router->top_dst->dst.weight : GLB_DBL_EPSILON);
}

/*! update decision context */
static inline void
router_update_ctx (glb_router_t* router)
{
    router->ctx.now        = time (NULL);
    router->ctx.retry      = router_retry_interval (router);
    router->ctx.min_weight = router_min_weight (router);
//    glb_log_debug ("router: top_dst = %p, min_weight = %7.2f",
//                   router->top_dst, router->ctx.min_weight);
}

static inline bool
router_uses_map (const glb_router_t* const router)
{
    return (router->cnf->policy >= GLB_POLICY_RANDOM);
}

/* return min_weight */
static void
router_redo_top (glb_router_t* router)
{
    int i;
    double const factor = 1.0 + GLB_DBL_EPSILON;
//    double top_weight = router_min_weight (router, now) * factor;
    double top_weight = router->ctx.min_weight * factor;

    /* If router->top_dst is not failed, it will be changed only if there is a
     * a non-failed dst with weight strictly higher than that of top_dst.
     * If router->top_dst is failed, then it will be changed only if there is
     * a non-failed dst with weight > 0. */

    for (i = 0; i < router->n_dst; i++)
    {
        router_dst_t* d = &router->dst[i];

        if (router_dst_is_good (d, top_weight, router->ctx.now,
                                router->ctx.retry))
        {
            router->top_dst = d;
            router->ctx.min_weight = d->dst.weight;
            top_weight = router->ctx.min_weight * factor;
        }
    }
}

static void
router_redo_map (glb_router_t* router)
{
    int i;

    // pass 1: calculate total weight of available destinations
    double total = 0.0;
    for (i = 0; i < router->n_dst; i++)
    {
        router_dst_t* d = &router->dst[i];

        if (router_dst_is_good (d, router->ctx.min_weight, router->ctx.now,
                                router->ctx.retry))
        {
            total += d->dst.weight;
            d->map = d->dst.weight;
        }
        else
        {
            d->map = 0.0;
        }
    }

    if (0.0 == total) return;

    // pass 2: normalize weights in a map
    double m = 0;
    for (i = 0; i < router->n_dst; i++)
    {
        router_dst_t* d = &router->dst[i];

        d->map = d->map / total + m;
        m = d->map;
    }
}

#ifdef GLBD
static inline double
router_dst_usage (router_dst_t* d)
/* +1 stands for what would be the usage of dst if we connect to it */
{ return (d->dst.weight / (d->conns + 1)); }
#endif

/*! return index of the deleted destination or negative error code*/
int
glb_router_change_dst (glb_router_t*             const router,
                       const glb_dst_t*          const dst,
                       glb_backend_thread_ctx_t* const probe_ctx)
{
    int           i;
    router_dst_t* d = NULL;

    GLB_MUTEX_LOCK (&router->lock);

    // try to find destination in the list
    for (i = 0; i < router->n_dst; i++) {
        if (glb_dst_is_equal(&((&router->dst[i])->dst), dst)) {
            d = &router->dst[i];
            break;
        }
    }

    // sanity check
    if (!d && dst->weight < 0) {
#ifdef GLBD
        char tmp[256];
        glb_dst_print (tmp, sizeof(tmp), dst);
        glb_log_warn ("Command to remove inexisting destination: %s", tmp);
#endif
        i = -ENONET;
        goto out;
    }

    if (!d || dst->weight < 0) {
        // cant remove/add destination while someone's connecting
        while (router->busy_count > 0) {
            router->wait_count++;
            pthread_cond_wait (&router->free, &router->lock);
            router->wait_count--;
            assert (router->wait_count >= 0);
        }
        assert (0 == router->busy_count);
    }

    router_dst_t* tmp = NULL;

    if (!d) { // add destination to the list

        assert (i == router->n_dst);

        tmp = realloc (router->dst, (router->n_dst + 1) * sizeof(router_dst_t));

        if (!tmp) {
            i = -ENOMEM;
        }
        else {
            router->dst = tmp;
            router->top_dst = NULL;
            d = router->dst + router->n_dst;
            router->n_dst++;
            d->dst       = *dst;
            d->probe_ctx = probe_ctx;
#ifdef GLBD
            d->conns     = 0;
            d->usage     = router_dst_usage(d);
#endif
            d->checked   = glb_time_now();
            d->failed    = 0;
        }
    }
    else if (dst->weight < 0) { // remove destination from the list

        assert (d);
        assert (i >= 0 && i < router->n_dst);

        router->top_dst = NULL;

#ifdef GLBD
        router->conns -= d->conns; assert (router->conns >= 0);
#endif
        if ((i + 1) < router->n_dst) {
            // it is not the last, copy the last one over
            *d = router->dst[router->n_dst - 1];
        }

        router->n_dst--;
        assert (router->n_dst >= 0);

        if (router->n_dst)
            router->rrb_next = router->rrb_next % router->n_dst;
        else
            router->rrb_next = 0;

        if (router->n_dst > 0)
            tmp = realloc (router->dst, router->n_dst * sizeof(router_dst_t));

        if (!tmp && (router->n_dst > 0)) {
            i = -ENOMEM; // this should actually be survivable, but no point
        }
        else {
            router->dst = tmp;
        }
    }
    else if (d->dst.weight != dst->weight) {
        // update weight and usage
        d->dst.weight = dst->weight;
#ifdef GLBD
        d->usage      = router_dst_usage (d);
#endif
    }
    else {
        goto out; // ineffective change
    }

    router_update_ctx (router);
    if (router->cnf->top) router_redo_top (router);
    if (router_uses_map(router)) router_redo_map (router);

    assert (router->n_dst >= 0);
out:
    if (router->wait_count > 0) pthread_cond_signal (&router->free);
    GLB_MUTEX_UNLOCK (&router->lock);
    return i;
}

static uint32_t
router_generate_seed()
{
    uint32_t seed = getpid();

    struct timeval t;
    gettimeofday (&t, NULL);

    seed ^= t.tv_sec;
    seed ^= t.tv_usec;

    return rand_r(&seed); // this should be sufficiently random and unique
}

static void
router_cleanup (glb_router_t* router)
{
    pthread_mutex_destroy (&router->lock);
    pthread_cond_destroy (&router->free);
    if (router->dst) free (router->dst);
    free (router);
}

glb_router_t*
glb_router_create (const glb_cnf_t* cnf/*size_t n_dst, glb_dst_t const dst[]*/)
{
#ifndef GLBD
    __glb_real_connect = dlsym(RTLD_NEXT, "__connect");
    if (!__glb_real_connect) return NULL;
#endif

    glb_router_t* ret = calloc (1, sizeof (glb_router_t));

    if (ret) {
        long i;

        pthread_mutex_init (&ret->lock, NULL);
        pthread_cond_init  (&ret->free, NULL);

        glb_sockaddr_init (&ret->sock_out, "0.0.0.0", 0); // client socket

        ret->cnf        = cnf;
        ret->busy_count = 0;
#ifdef GLBD
        ret->conns      = 0;
#endif
        ret->seed       = router_generate_seed();
        ret->rrb_next   = 0;
        ret->n_dst      = 0;
        ret->dst        = NULL;

        if (!cnf->watchdog) {
            for (i = 0; i < cnf->n_dst; i++) {
                if (glb_router_change_dst(ret, &cnf->dst[i], NULL) < 0) {
                    router_cleanup (ret);
                    return NULL;
                }
            }

            assert (ret->n_dst <= cnf->n_dst);
        }
    }

    return ret;
}

void
glb_router_destroy (glb_router_t* router)
{
    router_cleanup (router);
}

/* This will run extra destination check depending on uncheckd_intvl value */
static inline bool
router_dst_check (router_dst_t* const d,
                  glb_time_t    const uncheckd_intvl)
{
    glb_time_t now;
    return (0    == uncheckd_intvl                                 ||
            NULL == d->probe_ctx                                   ||
            ((now = glb_time_now()) - d->checked) < uncheckd_intvl ||
            router_dst_probe (d, now));
}

#ifdef GLBD
// find a ready destination with minimal usage
static router_dst_t*
router_choose_dst_least (glb_router_t* const router)
{
    router_dst_t* ret = NULL;

    if (router->n_dst > 0) {
        double max_usage = 0.0;
        int    i;

        for (i = 0; i < router->n_dst; i++) {
            router_dst_t* d = &router->dst[i];

            if (d->usage > max_usage &&
                router_dst_is_good (d, router->ctx.min_weight, router->ctx.now,
                                    router->ctx.retry)) {
                ret = d;
                max_usage = d->usage;
            }
        }

        if (!(ret && router_dst_check (ret, router->cnf->extra))) ret = NULL;
    }

    return ret;
}
#endif /* GLBD */

// find next suitable destination by round robin
static router_dst_t*
router_choose_dst_round (glb_router_t* const router)
{
    int offset;

    for (offset = 0; offset < router->n_dst; offset++)
    {
        router_dst_t* d = &router->dst[router->rrb_next];

        router->rrb_next = (router->rrb_next + 1) % router->n_dst;

        if (router_dst_is_good (d, router->ctx.min_weight, router->ctx.now,
                                router->ctx.retry) &&
            router_dst_check (d, router->cnf->extra))
            return d;
    }

    return NULL;
}

static inline router_dst_t*
router_choose_dst_single (glb_router_t* const router)
{
    return (router_top_dst_is_good (router) ? router->top_dst : NULL);
}

// find a ready destination by client source hint
static router_dst_t*
router_choose_dst_hint (glb_router_t* const router, uint32_t const hint)
{
    if (router->n_dst <= 0) return NULL;

#if OLD // old way
    /* First we attempt target predefined by hint (hint % router->n_dst).
     * If that fails, we iterate over the rest. But for every client
     * that falls onto that target we want to have a different iteration offset
     * (so that they don't all failover to the same destination), hence hint
     * is shifted, to get a fairly random (but deterministinc) offset.
     * That's why total number of attempts is N+1 */

    int      n = router->n_dst + 1;
    uint32_t i = hint;

    hint  = hint >> 8;

    while (n > 0) {
        i %= router->n_dst;

        router_dst_t* d = &router->dst[i];

        if (router_dst_is_good (d, now)) return d;

        i = hint + n;
        n -= 1;
    }
#else /* OLD */
    if (router_uses_map (router) &&
        router->map_failed != 0  &&
        difftime (router->ctx.now, router->map_failed) > router->ctx.retry)
    {
        router_redo_map (router);
        router->map_failed = 0;
    }

    // make sure it is strictly < 1.0
    double const m = ((double)hint) / 0xffffffff - router_div_prot;

    int i;
    for (i = 0; i < router->n_dst; i++)
    {
        router_dst_t* d = &router->dst[i];
        if (m < d->map && router_dst_check (d, router->cnf->extra)) return d;
        /* if every map is 0 we fall through and return NULL */
    }
#endif /* OLD */

    return NULL;
}

static inline uint32_t
router_random_hint (glb_router_t* router)
{
    uint32_t ret = rand_r(&router->seed);
    // the above returns only positive signed integers.
    // Need to expand it to 32 bits below.
    return ret ^ (ret << 1);
}

static inline router_dst_t*
router_choose_dst (glb_router_t* const router, uint32_t hint)
{
    router_update_ctx (router);

    if (router->cnf->top && router->top_failed != 0 &&
        difftime (router->ctx.now, router->top_failed) > router->ctx.retry)
    {
        router_redo_top (router);
        router->top_failed = 0;
    }

    router_dst_t* ret = NULL;

    switch (router->cnf->policy) {
    case GLB_POLICY_LEAST:
#ifdef GLBD
        ret = router_choose_dst_least (router);
#endif /* GLBD */
        break;
    case GLB_POLICY_ROUND:  ret = router_choose_dst_round (router); break;
    case GLB_POLICY_SINGLE: ret = router_choose_dst_single(router); break;
    case GLB_POLICY_RANDOM: hint = router_random_hint (router);
    case GLB_POLICY_SOURCE: ret = router_choose_dst_hint (router, hint);
    }

#ifdef GLBD
    if (GLB_LIKELY(ret != NULL)) {
        ret->conns++; router->conns++;
        ret->usage = router_dst_usage(ret);
    }
#endif /* GLBD */

    return ret;
}

#ifdef GLBD

#define glb_connect connect

int
glb_router_choose_dst (glb_router_t*   const router,
                       uint32_t        const src_hint,
                       glb_sockaddr_t* const dst_addr)
{
    int ret;

    GLB_MUTEX_LOCK (&router->lock);

    router_dst_t* const dst = router_choose_dst (router, src_hint);

    if (GLB_LIKELY(dst != NULL)) {
        *dst_addr = dst->dst.addr;
        ret = 0;
    }
    else {
        ret = -EHOSTDOWN;
    }

    GLB_MUTEX_UNLOCK (&router->lock);

    return ret;
}

#else /* GLBD */

#define glb_connect __glb_real_connect

#endif /* GLBD */

static inline void
router_dst_failed (glb_router_t* const router, router_dst_t* const dst)
{
    router->ctx.now   = time(NULL);
    router->ctx.retry = router_retry_interval (router);

    /* this is to avoid redundant redoing top|map if the dst was already marked
     * failed just now or was of lower than top weight and so didn't participate
     * in balancing. */
    bool const dst_was_good = router_dst_is_good (
        dst, router->ctx.min_weight, router->ctx.now, router->ctx.retry);

    dst->failed = router->ctx.now;

    if (dst_was_good)
    {
        if (dst == router->top_dst)
        {
            router->ctx.min_weight = GLB_DBL_EPSILON;
            router_redo_top (router);
            router->top_failed = dst->failed;
        }

        if (router_uses_map (router))
        {
            router_redo_map(router);
            router->map_failed = dst->failed;
        }
    }
}

// connect to a best destination, possiblly failing over to a next best
static int
router_connect_dst (glb_router_t*   const router,
                    int             const sock,
                    uint32_t        const hint,
                    glb_sockaddr_t* const addr)
{
    router_dst_t* dst;
    int  error    = EHOSTDOWN;
    int  ret      = -1;
    bool redirect = false;

    GLB_MUTEX_LOCK (&router->lock);

    router->busy_count++;

    // keep trying until we run out of destinations
    while ((dst = router_choose_dst (router, hint))) {

        GLB_MUTEX_UNLOCK (&router->lock);

        if (GLB_UNLIKELY(router->cnf->verbose)) {
            glb_sockaddr_str_t a = glb_sockaddr_to_str (&dst->dst.addr);
            glb_log_debug ("Connecting to %s", a.str);
        }

        ret = glb_connect (sock, (struct sockaddr*)&dst->dst.addr,
                           sizeof (dst->dst.addr));

        error = ret ? errno : 0;

        GLB_MUTEX_LOCK (&router->lock);

        if (error && error != EINPROGRESS) {
            // connect failed, undo usage count, update destination failed mark
#ifdef GLBD
            dst->conns--; router->conns--;
            assert (dst->conns >= 0);
            dst->usage = router_dst_usage(dst);
#endif
            if (GLB_UNLIKELY(router->cnf->verbose)) {
                glb_sockaddr_str_t a = glb_sockaddr_to_str (&dst->dst.addr);
                glb_log_warn ("Failed to connect to %s: %d (%s)",
                              a.str, error, strerror(error));
            }

            router_dst_failed (router, dst);
            redirect = true;
        }
        else {
            *addr = dst->dst.addr;
            if (GLB_UNLIKELY(redirect && router->cnf->verbose)) {
                glb_sockaddr_str_t a = glb_sockaddr_to_str (addr);
                glb_log_warn ("Redirecting to %s", a.str);
            }
            break;
        }
    }

    assert(dst != 0 || error >= 0);

    router->busy_count--;
    assert (router->busy_count >= 0);

    if (0 == router->busy_count && 0 < router->wait_count)
        pthread_cond_signal (&router->free);

    GLB_MUTEX_UNLOCK (&router->lock);

    return -error;
}

#ifdef GLBD
// returns 0 or negative error code
int
glb_router_connect (glb_router_t* router, const glb_sockaddr_t* src_addr,
                    glb_sockaddr_t* dst_addr, int* sock)
{
    int ret;

    /* Here it is assumed that this function is called only from one thread. */
    if (GLB_UNLIKELY(router->conns >= router->cnf->max_conn)) {
        glb_log_warn ("Maximum connection limit of %ld exceeded. Rejecting "
                      "connection attempt.", router->cnf->max_conn);
        *sock = -EMFILE;
        return *sock;
    }

    uint32_t hint = router->cnf->policy < GLB_POLICY_SOURCE ?
        0 : glb_sockaddr_hash (src_addr);

    if (!router->cnf->synchronous) {
        ret = glb_router_choose_dst (router, hint, dst_addr);
        if (!ret) ret = -EINPROGRESS;
        *sock = -1;
    }
    else {
        // prepare a socket
        uint32_t const ka_opt = router->cnf->keepalive * GLB_SOCK_KEEPALIVE;

        *sock = glb_socket_create (&router->sock_out,GLB_SOCK_NODELAY | ka_opt);

        if (*sock < 0) {
            glb_log_error ("glb_socket_create() failed");
            return *sock;
        }

        // attmept to connect until we run out of destinations
        ret = router_connect_dst (router, *sock, hint, dst_addr);

        // avoid socket leak
        if (ret < 0 /* && ret != -EINPROGRESS*/) {
            glb_log_error ("router_connect_dst() failed.");
            close (*sock);
            *sock = ret;
        }
    }

    return ret;
}

static inline int
router_disconnect (glb_router_t*         const router,
                   const glb_sockaddr_t* const dst,
                   bool                  const failed)
{
    int i;

    for (i = 0; i < router->n_dst; i++) {
        router_dst_t* d = &router->dst[i];
        if (glb_sockaddr_is_equal (&d->dst.addr, dst)) {
            d->conns--; router->conns--;
            assert(d->conns >= 0);
            d->usage = router_dst_usage (d);
            if (failed) router_dst_failed (router, d);
            break;
        }
    }

    return i;
}

void
glb_router_disconnect (glb_router_t*         const router,
                       const glb_sockaddr_t* const dst,
                       bool                  const failed)
{
    GLB_MUTEX_LOCK (&router->lock);

    int const i = router_disconnect (router, dst, failed);

    GLB_MUTEX_UNLOCK (&router->lock);

    if (i == router->n_dst) {
        glb_sockaddr_str_t a = glb_sockaddr_to_str (dst);
        glb_log_warn ("Attempt to disconnect from non-existing destination: %s",
                      a.str);
    }
}

int
glb_router_choose_dst_again (glb_router_t*   const router,
                             uint32_t        const src_hint,
                             glb_sockaddr_t* const dst_addr)
{
    int ret;

    GLB_MUTEX_LOCK (&router->lock);

#ifndef NDEBUG
    int const old_conns = router->conns;
#endif

    ret = router_disconnect (router, dst_addr, true);
    assert (ret != router->n_dst);

    router_dst_t* const dst = router_choose_dst (router, src_hint);

    assert (old_conns == router->conns);

    if (GLB_LIKELY(dst != NULL)) {
        *dst_addr = dst->dst.addr;
        ret = 0;
    }
    else {
        ret = -EHOSTDOWN;
    }

    GLB_MUTEX_UNLOCK (&router->lock);

    return ret;
}

size_t
glb_router_print_info (glb_router_t* router, char* buf, size_t buf_len)
{
    size_t len = 0;
    int    total_conns;
    int    n_dst;
    int    i;

    len += snprintf(buf + len, buf_len - len, "Router:\n"
                    "------------------------------------------------------\n"
                    "        Address       :   weight   usage    map  conns\n");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    GLB_MUTEX_LOCK (&router->lock);

    for (i = 0; i < router->n_dst; i++) {
        router_dst_t* d = &router->dst[i];
        glb_sockaddr_str_t addr = glb_sockaddr_to_astr (&d->dst.addr);

        if (router_uses_map (router)) {
            len += snprintf (buf + len, buf_len - len,
                             "%s : %8.3f %7.3f %7.3f %5d\n",
                             addr.str,
                             d->dst.weight, 1.0 - (d->usage/d->dst.weight),
                             d->map, d->conns);
        }
        else {
            len += snprintf (buf + len, buf_len - len,
                             "%s : %8.3f %7.3f    N/A  %5d\n",
                             addr.str,
                             d->dst.weight, 1.0 - (d->usage/d->dst.weight),
                             d->conns);
        }

        if (len == buf_len) {
            buf[len - 1] = '\0';
            GLB_MUTEX_UNLOCK (&router->lock);
            return (len - 1);
        }
    }

    n_dst = router->n_dst;
    total_conns = router->conns;

    GLB_MUTEX_UNLOCK (&router->lock);

    len += snprintf (buf + len, buf_len - len,
                     "------------------------------------------------------\n"
                     "Destinations: %d, total connections: %d of %d max\n",
                     n_dst, total_conns, router->cnf->max_conn);

    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    return len;
}

#else /* GLBD */

int glb_router_connect(glb_router_t* const router, int const sockfd)
{
    glb_sockaddr_t dst;

    uint32_t hint = router->seed;
    // random hint will be generated in router_connect_dst()

    // need to temporarily make socket blocking
    int const orig_flags = fcntl (sockfd, F_GETFL);
    if (orig_flags >= 0) {
        int const block_flags = orig_flags & (~O_NONBLOCK);

        if (orig_flags != block_flags) fcntl (sockfd, F_SETFL, block_flags);

        int const ret = router_connect_dst (router, sockfd, hint, &dst);

        assert (ret <= 0);

        if (orig_flags != block_flags) fcntl (sockfd, F_SETFL, orig_flags);

        if (GLB_UNLIKELY(ret < 0)) {
            errno = -ret;
        }
        else {
            if (orig_flags == block_flags) {
                // socket was blocking, no voodoo was made.
                return 0;
            }
            else {
                // socket was non-blocking, temporarily reverted to blocking
                // for non-blocking socket connect() is expected to return -1
                // and set errno to EINPROGRESS
                errno = EINPROGRESS;
            }
        }
    }

    return -1;
}

size_t
glb_router_print_info (glb_router_t* router, char* buf, size_t buf_len)
{
    size_t len = 0;
    int    n_dst;
    int    i;

    len += snprintf(buf + len, buf_len - len, "Router:\n"
                    "-----------------------------------------\n"
                    "        Address       :   weight    map  \n");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    GLB_MUTEX_LOCK (&router->lock);

    for (i = 0; i < router->n_dst; i++) {
        router_dst_t* d = &router->dst[i];
        glb_sockaddr_str_t addr = glb_sockaddr_to_astr (&d->dst.addr);

        len += snprintf (buf + len, buf_len - len, "%s : %8.3f %7.3f\n",
                         addr.str, d->dst.weight, d->map);

        if (len == buf_len) {
            buf[len - 1] = '\0';
            GLB_MUTEX_UNLOCK (&router->lock);
            return (len - 1);
        }
    }

    n_dst = router->n_dst;

    GLB_MUTEX_UNLOCK (&router->lock);

    len += snprintf (buf + len, buf_len - len,
                     "-----------------------------------------\n"
                     "Destinations: %d\n", n_dst);

    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    return len;
}

#endif /* GLBD */

