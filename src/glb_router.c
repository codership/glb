/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * NOTE: connection count and usage make sense only for standalone balancer
 *       so all operations on them are #ifdef GLBD ... #endif
 *
 * $Id$
 */

#define _GNU_SOURCE 1 /* for function overloading */

#include "glb_log.h"
#include "glb_cmd.h"
#include "glb_misc.h"
#include "glb_socket.h"
#include "glb_router.h"

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h> // for close()
#include <time.h>
#include <sys/time.h>

#ifdef GLBD
#  include <stdio.h>
#else /* GLBD */
#  include <dlfcn.h>
static int (*__glb_real_connect) (int                    sockfd,
                                  const struct sockaddr* addr,
                                  socklen_t              addrlen) = NULL;
#endif /* GLBD */

typedef struct router_dst
{
    glb_dst_t dst;
#ifdef GLBD
    double    usage;  // usage measure: weight/(conns + 1) - bigger wins
#endif
    double    map;    // hard to explain
    time_t    failed; // last time connection to this destination failed
#ifdef GLBD
    int       conns;  // how many connections use this destination
#endif
} router_dst_t;

struct glb_router
{
    const volatile glb_cnf_t* cnf;
    glb_sockaddr_t  sock_out; // outgoing socket address
    pthread_mutex_t lock;
    long            busy_count;
    long            wait_count;
    pthread_cond_t  free;
#ifdef GLBD
    int             conns;
#endif
    unsigned int    seed;     // seed for rng
    int             rrb_next; // round-robin cursor
    int             n_dst;
    time_t          map_failed;
    router_dst_t*   dst;
};

static const double router_div_prot = 1.0e-09; // protection against div by 0

// seconds (should be >= 1 due to time_t precision)
#define ROUTER_RETRY_INTERVAL(router) \
(glb_time_approx_seconds(router->cnf->interval) + 1)

static inline bool
router_dst_is_good (const router_dst_t* const d,
                    time_t              const now,
                    long                const retry)
{
    return ((d->dst.weight > 0.0) && (difftime (now, d->failed) > retry));
}

static void
router_redo_map (glb_router_t* router)
{
    time_t const now   = time(NULL);
    long   const retry = ROUTER_RETRY_INTERVAL(router);
    int i;

    // pass 1: calculate total weight of available destinations
    double total = 0.0;
    for (i = 0; i < router->n_dst; i++)
    {
        router_dst_t* d = &router->dst[i];

        if (router_dst_is_good (d, now, retry))
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
{ return (d->dst.weight / (d->conns + router_div_prot)); }
#endif

/*! return index of the deleted destination or negative error code*/
int
glb_router_change_dst (glb_router_t* router, const glb_dst_t* dst)
{
    int           i;
    void*         tmp;
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
        GLB_MUTEX_UNLOCK (&router->lock);
#ifdef GLBD
        char tmp[256];
        glb_dst_print (tmp, sizeof(tmp), dst);
        glb_log_warn ("Command to remove inexisting destination: %s", tmp);
#endif
        return -EADDRNOTAVAIL;
    }

    if (!d || dst->weight < 0) {
        // cant remove/add destination while someone's connecting
        while (router->busy_count > 0) {
            router->wait_count++;
            pthread_cond_wait (&router->free, &router->lock);
            router->wait_count--;
            assert (router->wait_count >= 0);
        }
    }

    if (!d) { // add destination to the list

        assert (i == router->n_dst);

        tmp = realloc (router->dst, (router->n_dst + 1) * sizeof(router_dst_t));

        if (!tmp) {
            i = -ENOMEM;
        }
        else {
            router->dst = tmp;
            d = router->dst + router->n_dst;
            router->n_dst++;
            d->dst    = *dst;
#ifdef GLBD
            d->conns  = 0;
            d->usage  = router_dst_usage(d);
#endif
            d->failed = 0;
        }
    }
    else if (dst->weight < 0) { // remove destination from the list

        assert (d);
        assert (i >= 0 && i < router->n_dst);
#ifdef GLBD
        router->conns -= d->conns; assert (router->conns >= 0);
#endif
        if ((i + 1) < router->n_dst) {
            // it is not the last, copy the last one over
            *d = router->dst[router->n_dst - 1];
//            router_dst_t* next = d + 1;
//            size_t len = (router->n_dst - i - 1)*sizeof(router_dst_t);
//            memmove (d, next, len);
        }

        router->n_dst--;
        assert (router->n_dst >= 0);
        if (router->n_dst)
            router->rrb_next = router->rrb_next % router->n_dst;
        else
            router->rrb_next = 0;

        tmp = realloc (router->dst, router->n_dst * sizeof(router_dst_t));

        if (!tmp && (router->n_dst > 1)) {
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

    router_redo_map (router);

    assert (router->n_dst >= 0);
    assert (0 == router->busy_count);

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

    glb_router_t* ret = malloc (sizeof (glb_router_t));

    if (ret) {
        long i;

        pthread_mutex_init (&ret->lock, NULL);
        pthread_cond_init  (&ret->free, NULL);

        glb_socket_addr_init (&ret->sock_out, "0.0.0.0", 0); // client socket

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
                if (glb_router_change_dst(ret, &cnf->dst[i]) < 0) {
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

#ifdef GLBD
// find a ready destination with minimal usage
static router_dst_t*
router_choose_dst_least (glb_router_t* router)
{
    router_dst_t* ret = NULL;

    if (router->n_dst > 0) {
        double max_usage = 0.0;
        int    i;
        time_t now = time(NULL);

        for (i = 0; i < router->n_dst; i++) {
            router_dst_t* d = &router->dst[i];

            if (d->usage > max_usage &&
                difftime (now, d->failed) > ROUTER_RETRY_INTERVAL(router)) {
                ret = d;
                max_usage = d->usage;
            }
        }
    }

    return ret;
}
#endif /* GLBD */

// find next suitable destination by round robin
static router_dst_t*
router_choose_dst_round (glb_router_t* router)
{
    time_t const now   = time(NULL);
    long   const retry = ROUTER_RETRY_INTERVAL(router);
    int    offset;

    for (offset = 0; offset < router->n_dst; offset++)
    {
        router_dst_t* d = &router->dst[router->rrb_next];

        router->rrb_next = (router->rrb_next + 1) % router->n_dst;

        if (router_dst_is_good (d, now, retry)) return d;
    }

    return NULL;
}

// find a ready destination by client source hint
static router_dst_t*
router_choose_dst_hint (glb_router_t* router, uint32_t hint)
{
    if (router->n_dst <= 0) return NULL;

    time_t now = time(NULL);

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
    if (router->map_failed != 0 &&
        difftime (now, router->map_failed) > ROUTER_RETRY_INTERVAL(router))
    {
        router_redo_map (router);
        router->map_failed = 0;
    }

    // make sure it is strictly < 1.0
    double const m = ((double)hint) / 0xffffffff - router_div_prot;

    int i;
    for (i = 0; i < router->n_dst; i++)
    {
        if (m < router->dst[i].map) return &router->dst[i];
        /* if every map is 0 we fall through and return NULL */
    }
#endif /* OLD */

    return NULL;
}

#ifdef GLBD

#define glb_connect connect

static inline router_dst_t*
router_choose_dst (glb_router_t* router, uint32_t hint)
{
    switch (router->cnf->policy) {
    case GLB_POLICY_LEAST:  return router_choose_dst_least (router);
    case GLB_POLICY_ROUND:  return router_choose_dst_round (router);
    case GLB_POLICY_RANDOM:
    case GLB_POLICY_SOURCE: return router_choose_dst_hint  (router, hint);
    }
    return NULL;
}

#else /* GLBD */

#define glb_connect __glb_real_connect

static inline router_dst_t*
router_choose_dst (glb_router_t* router, uint32_t hint)
{
    switch (router->cnf->policy) {
    case GLB_POLICY_LEAST:  return NULL;
    case GLB_POLICY_ROUND:  return router_choose_dst_round (router);
    case GLB_POLICY_RANDOM:
    case GLB_POLICY_SOURCE: return router_choose_dst_hint  (router, hint);
    }
    return NULL;
}

#endif /* GLBD */

// connect to a best destination, possiblly failing over to a next best
static int
router_connect_dst (glb_router_t*   const router,
                    int             const sock,
                    uint32_t        const hint,
                    glb_sockaddr_t* const addr)
{
    router_dst_t* dst;
    int  error    = EHOSTDOWN;
    int  ret;
    bool redirect = false;

    GLB_MUTEX_LOCK (&router->lock);

    router->busy_count++;

    // keep trying until we run out of destinations
    while ((dst = router_choose_dst (router, hint))) {
#ifdef GLBD
        dst->conns++; router->conns++;
        dst->usage = router_dst_usage(dst);
#endif
        GLB_MUTEX_UNLOCK (&router->lock);

        ret = glb_connect (sock, (struct sockaddr*)&dst->dst.addr,
                           sizeof (dst->dst.addr));

        GLB_MUTEX_LOCK (&router->lock);

        if (ret != 0) {
            error = errno;
            // connect failed, undo usage count, update destination failed mark
#ifdef GLBD
            dst->conns--; router->conns--;
            assert (dst->conns >= 0);
            dst->usage = router_dst_usage(dst);
#endif
            glb_log_warn ("Failed to connect to %s: %d (%s)",
                          glb_socket_addr_to_string (&dst->dst.addr),
                          error, strerror(error));
            dst->failed = time(NULL);
            router_redo_map(router);
            router->map_failed = dst->failed;
            redirect = true;
        }
        else {
            *addr = dst->dst.addr;
            if (redirect) {
                glb_log_warn ("Redirecting to %s",
                              glb_socket_addr_to_string (addr));
            }
            error = 0; // return success
            break;
        }
    }
    assert(dst != 0 || error != 0);

    router->busy_count--;
    assert (router->busy_count >= 0);

    if (0 == router->busy_count && 0 < router->wait_count)
        pthread_cond_signal (&router->free);

    GLB_MUTEX_UNLOCK (&router->lock);

    return -error; // all attempts failed, return last errno
}

static inline uint32_t
router_random_hint (glb_router_t* router)
{
    uint32_t ret = rand_r(&router->seed);
    // the above returns only positive signed integers.
    // Need to expand it to 32 bits below.
    return ret ^ (ret << 1);
}

#ifdef GLBD
// returns socket number or negative error code
int
glb_router_connect (glb_router_t* router, const glb_sockaddr_t* src_addr,
                    glb_sockaddr_t* dst_addr)
{
    int sock, ret;

    /* Here it is assumed that this function is called only from one thread. */
    if (router->conns >= router->cnf->max_conn) {
        glb_log_warn ("Maximum connection limit of %ld exceeded. Rejecting "
                      "connection attempt.", router->cnf->max_conn);
        return -EMFILE;
    }

    // prepare a socket
    sock = glb_socket_create (&router->sock_out, GLB_SOCK_NODELAY);
    if (sock < 0) {
        glb_log_error ("glb_socket_create() failed");
        return sock;
    }

    uint32_t hint = 0;
    switch (router->cnf->policy)
    {
    case GLB_POLICY_LEAST:
    case GLB_POLICY_ROUND:  break;
    case GLB_POLICY_RANDOM: hint = router_random_hint(router); break;
    case GLB_POLICY_SOURCE: hint = glb_socket_addr_hash(src_addr); break;
    };

    // attmept to connect until we run out of destinations
    ret = router_connect_dst (router, sock, hint, dst_addr);

    // avoid socket leak
    if (ret < 0) {
        glb_log_debug ("router_connect_dst() failed.");
        close (sock);
        sock = ret;
    }

    return sock;
}

void
glb_router_disconnect (glb_router_t* router, const glb_sockaddr_t* dst)
{
    long i;

    GLB_MUTEX_LOCK (&router->lock);

    for (i = 0; i < router->n_dst; i++) {
        router_dst_t* d = &router->dst[i];
        if (glb_socket_addr_is_equal (&d->dst.addr, dst)) {
            d->conns--; router->conns--;
            assert(d->conns >= 0);
            d->usage = router_dst_usage(d);
            break;
        }
    }

    if (i == router->n_dst) {
        glb_log_warn ("Attempt to disconnect from non-existing destination: %s",
                      glb_socket_addr_to_string(dst));
    }

    GLB_MUTEX_UNLOCK (&router->lock);
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

        len += snprintf (buf + len, buf_len - len,
                         "%s : %8.3f %7.3f %7.3f %5d\n",
                         glb_socket_addr_to_string(&d->dst.addr),
                         d->dst.weight, 1.0/(d->usage + 1.0), d->map,
                         d->conns);
        if (len == buf_len) {
            buf[len - 1] = '\0';
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

int __glb_router_connect(glb_router_t* const router, int const sockfd)
{
    glb_sockaddr_t dst;

    uint32_t hint =
        GLB_POLICY_RANDOM == router->cnf->policy ?
        router_random_hint(router) : router->seed;

    return router_connect_dst (router, sockfd, hint, &dst);
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

        len += snprintf (buf + len, buf_len - len, "%s : %8.3f %7.3f\n",
                         glb_socket_addr_to_string(&d->dst.addr),
                         d->dst.weight, d->map);
        if (len == buf_len) {
            buf[len - 1] = '\0';
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

