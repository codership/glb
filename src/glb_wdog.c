/*
 * Copyright (C) 2012-2013 Codership Oy <info@codership.com>
 *
 * Watchdog implementation: main loop module.
 *
How it works:
     ,---------.
     | control |
     | thread  |
     `---------'
          |
add/remove destinations
          V                 query destination
,--------------------.    ,------------------.   ,-------------.
|                    |<-->|  backend thread  |-->| destination |
|                    |    `------------------'   `-------------'
| watchdog main loop |    ,------------------.   ,-------------.
|                    |<-->|  backend thread  |-->| destination |
|     (collect       |    `------------------'   `-------------'
| destination data)  |     . . .
|                    |    ,------------------.   ,-------------.
|                    |<-->|  backend thread  |-->| destination |
`--------------------'    `------------------'   `-------------'
          |
  destination changes
          V
  ,--------------.
  |    router    |
  |  (dispatch   |
  | connections) |
  `--------------'

 * $Id$
 */

#include "glb_wdog.h"
#include "glb_wdog_backend.h"
#include "glb_wdog_exec.h"
#include "glb_dst.h"
#include "glb_log.h"
#include "glb_socket.h"
#include "glb_misc.h"

#include <math.h>     // fabs()
#include <assert.h>
#include <errno.h>
#include <time.h>     // nanosleep()
#include <sys/time.h> // gettimeofday()

#undef NDEBUG // for now

#define WDOG_MAX_FAIL_COUNT 8

typedef struct wdog_dst
{
    glb_wdog_check_t          result;
    glb_dst_t                 dst;
    double                    weight;
    glb_backend_thread_ctx_t* ctx;      //! backend thread context
    int                       fail_count;
    bool                      memb_changed;
    bool                      explicit; //! was added explicitly, never remove
} wdog_dst_t;

struct glb_wdog
{
    glb_backend_t    backend;
    const glb_cnf_t* cnf;
    glb_router_t*    router;
    glb_pool_t*      pool;
    pthread_t        thd;
    pthread_mutex_t  lock;
    pthread_cond_t   cond;
    bool             quit;
    bool             join;
    long long        interval; // nsec
    struct timespec  next;
    int              n_dst;
    wdog_dst_t*      dst;
};

static glb_backend_thread_ctx_t*
wdog_backend_thread_ctx_create (glb_backend_ctx_t* const backend,
                                const glb_dst_t*   const dst,
                                long long          const interval)
{
    glb_sockaddr_str_t const h    = glb_sockaddr_get_host (&dst->addr);
    short              const port = glb_sockaddr_get_port (&dst->addr);

    char* const host = strdup (h.str);

    if (host)
    {
        glb_backend_thread_ctx_t* const ret = calloc (1, sizeof(*ret));

        if (ret) {
            glb_log_debug ("Created context for %s:%hu", host, port);
            ret->backend = backend;
            pthread_mutex_init (&ret->lock, NULL);
            pthread_cond_init  (&ret->cond, NULL);
            ret->host = host;
            ret->port = port;
            ret->interval = interval;
            return ret;
        }

        free (host);
    }

    return NULL;
}

static void
wdog_backend_thread_ctx_destroy (glb_backend_thread_ctx_t* ctx)
{
    free (ctx->host);
    free ((void*)ctx->result.others);
    pthread_mutex_destroy (&ctx->lock);
    pthread_cond_destroy  (&ctx->cond);
    free (ctx);
}

static int
wdog_change_dst (glb_wdog_t*      const wdog,
                 const glb_dst_t* const dst,
                 bool             const explicit)
{
    int         i;
    void*       tmp;
    wdog_dst_t* d = NULL;

    // try to find destination in the list
    for (i = 0; i < wdog->n_dst; i++) {
        if (glb_dst_is_equal(&wdog->dst[i].dst, dst)) {
            d = &wdog->dst[i];
            break;
        }
    }

    // sanity check
    if (!d && dst->weight < 0) {
        char tmp[256];
        glb_dst_print (tmp, sizeof(tmp), dst);
        glb_log_warn ("Command to remove inexisting destination: %s", tmp);
        return -EADDRNOTAVAIL;
    }

    if (!d) { // add destination

        assert (0 <= dst->weight);
        assert (i == wdog->n_dst);

        if (GLB_UNLIKELY (wdog->cnf->verbose)) {
            char tmp[256];
            glb_dst_print (tmp, sizeof(tmp), dst);
            glb_log_debug ("Adding '%s' at pos. %d", tmp, i);
        }

        glb_backend_thread_ctx_t* ctx =
            wdog_backend_thread_ctx_create (wdog->backend.ctx, dst,
                                            wdog->cnf->interval);

        if (!ctx) {
            i = -ENOMEM;
        }
        else {
            tmp = realloc (wdog->dst, (wdog->n_dst + 1) * sizeof(wdog_dst_t));

            if (!tmp) {
                wdog_backend_thread_ctx_destroy (ctx);
                i = -ENOMEM;
            }
            else {
                bool success = false;
                wdog->dst = tmp;

                GLB_MUTEX_LOCK (&ctx->lock);
                {
                    pthread_create (&ctx->id, NULL, wdog->backend.thread, ctx);
                    pthread_cond_wait (&ctx->cond, &ctx->lock);
                    success = !ctx->join;
                }
                GLB_MUTEX_UNLOCK (&ctx->lock);

                if (success)
                {
                    glb_log_debug ("Backend thread for '%s:%hu' started.",
                                   ctx->host, ctx->port);
                    d = wdog->dst + wdog->n_dst;
                    wdog->n_dst++;
                    memset (d, 0, sizeof(*d));
                    d->explicit = explicit;
                    d->dst      = *dst;
                    d->weight   = -1.0; // not yet in router list
                    d->ctx      = ctx;
                }
                else {
                    i = -ctx->errn;
                    pthread_join (ctx->id, NULL);
                    glb_log_error ("Backend thread for '%s:hu' failed: %d (%s)",
                                   ctx->host, ctx->port, -i, strerror (-i));
                    wdog_backend_thread_ctx_destroy (ctx);
                }
            }
        }
    }
    else if (dst->weight < 0) // remove destination from the list
    {
        assert (d);
        assert (i >= 0 && i < wdog->n_dst);

        if (explicit || !d->explicit)
        {
            glb_log_debug ("Signaling thread for '%s:hu' to quit.",
                           d->ctx->host, d->ctx->port);
            GLB_MUTEX_LOCK (&d->ctx->lock);
            d->ctx->quit = true;
            pthread_cond_signal (&d->ctx->cond);
            GLB_MUTEX_UNLOCK (&d->ctx->lock);
            /* thread will be joined context will be cleaned up later */
        }
        else
        {
            // no right to remove, just mark it inaccessible
//            glb_log_info ("Marking '%s:%hu' inaccessible",
//                          d->ctx->host, d->ctx->port);
//            d->dst.weight = -1.0; - this way we will loose original weight
        }
    }
    else {
        assert (d);
        assert (i >= 0 && i < wdog->n_dst);

        /* keep destination in the list as long as it is being referred to */
        d->fail_count = 0;

        if (explicit) {
            d->explicit   = true;
            d->dst.weight = dst->weight;
        }
        else if (!d->explicit) {
            d->dst.weight = dst->weight;
        }
    }

    return i;
}

int
glb_wdog_change_dst (glb_wdog_t*      const wdog,
                     const glb_dst_t* const dst)
{
    int ret;

    GLB_MUTEX_LOCK (&wdog->lock);

    ret = wdog_change_dst (wdog, dst, true);

    GLB_MUTEX_UNLOCK (&wdog->lock);

    return ret;
}


static int
wdog_backend_factory (const glb_cnf_t* cnf,
                      glb_backend_t*   backend)
{
    memset (backend, 0, sizeof (*backend));

    assert (cnf->watchdog);

    char* spec = strchr(cnf->watchdog, ':'); // seek for first colon

    if (spec)
    {
        *spec = '\0'; // separate watchdog id string
        spec++;       // this is passed to backend
    }

    if (!strcmp (cnf->watchdog, "dummy"))
    {
        return glb_backend_dummy_init (backend, spec);
    }
    else if (!strcmp (cnf->watchdog, "exec"))
    {
        return glb_backend_exec_init (backend, spec);
    }
    else
    {
        glb_log_error ("'%s' watchdog not implemented.", cnf->watchdog);
        return -ENOSYS;
    }
}

static inline int
wdog_copy_result (wdog_dst_t* const d, double* const max_lat, int const lf)
{
    double const old_lat    = d->result.latency;
    char*  const others     = d->result.others;
    size_t const others_len = d->result.others_len;

    GLB_MUTEX_LOCK (&d->ctx->lock);

    glb_wdog_check_t* res = &d->ctx->result;

    d->result = *res;
    res->ready = false;

    // restore original buffer
    d->result.others     = others;
    d->result.others_len = others_len;

    if (d->result.ready) {
        if (GLB_DST_NOTFOUND == d->result.state) {
            d->fail_count++;
            if (!d->explicit &&  // if auto-discovered - schedule for cleanup
                d->fail_count > WDOG_MAX_FAIL_COUNT) {
                glb_log_debug ("Fail count for '%s:%hu' exceeded %d. "
                               "Scheduling for removal.",
                               d->ctx->host, d->ctx->port, WDOG_MAX_FAIL_COUNT);
                d->ctx->quit = true;
                pthread_cond_signal (&d->ctx->cond);
            }
        }
        else { // remote destination is live, handle others string
            bool changed_length = false;

            if (others_len < res->others_len ||
                others_len > (res->others_len * 2)) {
                // buffer size is too different, reallocate
                d->result.others = realloc (others, res->others_len + 1);
                if (!d->result.others && res->others_len > 0) {
                    // this is pretty much fatal, but we'll try
                    free (others);
                    d->result.others_len = 0;
                }
                else {
                    changed_length = true;
                    d->result.others_len = res->others_len;
                }
            }

            assert (d->result.others || 0 == d->result.others_len);
            assert (NULL == d->result.others || d->result.others_len);

            if (res->others_len > 0 &&
                d->result.others_len >= res->others_len &&
                (changed_length ||
                 strcmp(d->result.others, res->others)))
            {
                glb_log_debug("Setting memb_changed because changed_length: %d "
                              "or strcmp(\n old: '%s'\n new: '%s'): %d",
                              changed_length, d->result.others, res->others,
                              strcmp(d->result.others, res->others));
                d->memb_changed = true;
                strcpy (d->result.others, res->others);
            }
        }
    }

    GLB_MUTEX_UNLOCK (&d->ctx->lock);

    if (d->result.ready && GLB_DST_READY == d->result.state) {
        // smooth latency measurement with the previous one
        d->result.latency = (d->result.latency + old_lat * lf) / (lf + 1);
        if (*max_lat < d->result.latency) *max_lat = d->result.latency;

//        glb_sockaddr_str_t a = glb_sockaddr_to_str (&d->dst.addr);
//        glb_log_debug ("%s latency: %6.4f", a.str, d->result.latency);

    }
    else {
        // preserve previously measured latency
        d->result.latency = old_lat;
    }

    return 0;
}

// returns latency adjusted weight
static inline double
wdog_result_weight (wdog_dst_t* const d, double const max_lat,
                    bool const use_latency)
{
    assert (d->result.ready); // this must be called only for fresh data

    switch (d->result.state)
    {
    case GLB_DST_NOTFOUND:
    case GLB_DST_NOTREADY:
        return -1.0;
    case GLB_DST_AVOID:
        return 0.0;
    case GLB_DST_READY:
        if (max_lat > 0 && use_latency)
            return d->dst.weight * max_lat / d->result.latency;
        else
            return d->dst.weight;
    }

    return 0.0;
}

static void
wdog_dst_free (wdog_dst_t* d)
{
    wdog_backend_thread_ctx_destroy (d->ctx);
    free (d->result.others);
}

static int
wdog_process_membership_change (glb_wdog_t* wdog, const char* const memb_str)
{
    assert (wdog);
    assert (memb_str);
//    glb_log_debug ("Processing new membership: %s", memb_str);

    char* const tmp_str = strdup (memb_str);

    if (!tmp_str)
    {
        glb_log_error ("Could not allocate %zu bytes for memb_str copy.",
                       strlen(memb_str));
        return -ENOMEM;
    }

    const char** memb_list;
    int          memb_num;

    uint16_t default_port = glb_sockaddr_get_port (&wdog->cnf->inc_addr);

    if (glb_parse_token_string (tmp_str, &memb_list, &memb_num, ','))
    {
        free (tmp_str);
        return -ENOMEM;
    }

    int i;

    for (i = 0; i < memb_num; i++)
    {
        int err;
        glb_dst_t dst;

        err = glb_dst_parse (&dst, memb_list[i], default_port);
        if (err < 0)
        {
            glb_log_error ("Failed to parse destination '%s': %d (%s). "
                           "Skipping.", memb_list[i], -err, strerror(-err));
            continue;
        }

        if (GLB_UNLIKELY(wdog->cnf->verbose))
        {
            glb_sockaddr_str_t a = glb_sockaddr_to_str (&dst.addr);

            if (strcmp(memb_list[i], a.str))
            {
                glb_log_debug ("'%s' -> '%s'", memb_list[i], a.str);
            }
        }

        err = wdog_change_dst (wdog, &dst, false);

        if (err < 0)
        {
            glb_sockaddr_str_t a = glb_sockaddr_to_str (&dst.addr);

            glb_log_error ("Failed to adjust destination '%s': %d (%s).",
                           a.str, -err, strerror (-err));
        }
#if 0 // does not seem to be necessary
        else if (GLB_UNLIKELY(wdog->cnf->verbose))
        {
            glb_sockaddr_str_t a = glb_sockaddr_to_str (&dst.addr);

            glb_log_debug ("Adjusted destination '%s' at pos. %d.",
                           a.str, err);
        }
#endif // 0
    }

    free (memb_list);
    free (tmp_str);

    return 0;
}

// collects and processes results, returns the number of results collected
static int
wdog_collect_results (glb_wdog_t* const wdog)
{
#ifdef GLBD
    glb_log_debug ("main loop collecting...");
#endif
    double max_lat = 0.0;
    int results = 0;

    int i;
    for (i = 0; i < wdog->n_dst; i++)
    {
        wdog_copy_result (&wdog->dst[i], &max_lat, wdog->cnf->lat_factor);
    }

    int const old_n_dst = wdog->n_dst;
    int memb_source = -1;

    for (i = wdog->n_dst - 1; i >= 0; i--) // reverse order for ease of cleanup
    {
        wdog_dst_t* d = &wdog->dst[i];
        double new_weight;

        if (d->ctx->join) {
            pthread_join (d->ctx->id, NULL);
            glb_log_debug ("Joined thread for '%s:%hu'",
                           d->ctx->host, d->ctx->port);
            wdog_dst_free (d);
            wdog->n_dst--;
            if (i < wdog->n_dst) {
                // not the last in the list, copy the last one over this
                *d = wdog->dst[wdog->n_dst];
            }
            continue;
        }

        if (d->result.ready) {
            results++;
            new_weight = wdog_result_weight (d, max_lat,
                                             wdog->cnf->lat_factor > 0);

            if (wdog->cnf->discover && memb_source < 0 &&
                GLB_DST_READY == d->result.state && d->result.others)
            {
                /* This will restrict new memb check only to the first dst
                 * entry which is GLB_DST_READY. For Galera this should be
                 * enough as every node should report the same membership -
                 * at least "eventually". */
                memb_source = i;
            }
        }
        else {
            // have not heard from the backend thread, put dest on hold
            if (d->weight >= 0.0) {
                new_weight = 0.0;
                if (d->result.state > GLB_DST_AVOID)
                    d->result.state = GLB_DST_AVOID;
            }
            else {
                new_weight = d->weight;
                if (d->result.state > GLB_DST_NOTREADY)
                    d->result.state = GLB_DST_NOTREADY;
            }
        }

        static double const WEIGHT_TOLERANCE = 0.1; // 10%

        if (new_weight != d->weight &&
            (new_weight <= 0.0 ||
             fabs(d->weight/new_weight - 1.0) > WEIGHT_TOLERANCE)) {
            glb_dst_t dst = d->dst;
            dst.weight = new_weight;
            int ret = glb_router_change_dst (wdog->router, &dst, d->ctx);

            glb_log_debug ("Changing weight for '%s:%hu': %6.3f -> %6.3f:  "
                           "%d (%s)",
                           d->ctx->host, d->ctx->port, d->weight, new_weight,
                           ret, strerror (ret > 0 ? 0 : -ret));

            if (ret >= 0) {
                if (new_weight < 0.0 && wdog->pool) { // clean up the pool!
                    glb_pool_drop_dst (wdog->pool, &d->dst.addr);
                }
                d->weight = new_weight;
            }
        }
    }

    if (old_n_dst != wdog->n_dst) {
        // removed some destinations
        void* const tmp = realloc (wdog->dst, wdog->n_dst * sizeof(wdog_dst_t));
        if (tmp || 0 == wdog->n_dst) wdog->dst = tmp;
    }

    /* It looks like the following must be run on every wdog_collect_results()
     * call to reset fail counts on all destinations that are still referenced
     * in d->result.others. Otherwise a destination may be removed from the
     * list and forgotten until (and if) others change again, even if it
     * becomes reachable meanwhile. */
    if (memb_source >= 0 /* && wdog->dst[memb_source].memb_changed */)
    {
        wdog_dst_t* d = &wdog->dst[memb_source];

        assert (d->result.others);

//        glb_log_debug ("Adjusting dest. because memb_source: %d, "
//                       "memb_changed: %d", memb_source, d->memb_changed);

        d->memb_changed = false; // reset trigger

        /* this can change wdog->dst, so it must go last */
        wdog_process_membership_change (wdog, d->result.others);
    }

    return results;
}


static void*
wdog_main_loop (void* arg)
{
    glb_wdog_t* wdog = arg;

    GLB_MUTEX_LOCK(&wdog->lock);

    if (wdog->n_dst > 0) {
        // since we're just starting and we have non-empty destination list,
        // try to get at least one destination confirmed ASAP before going into
        // main loop
        int n = wdog_collect_results (wdog);
        int i = 10;
        while (!n && i--) {
            struct timespec t = { 0, 100000000 }; // 0.1 sec
            nanosleep (&t, NULL);
            n = wdog_collect_results (wdog);
        }
    }

    pthread_cond_signal (&wdog->cond);

    wdog->next = glb_timespec_now();

    while (!wdog->quit)
    {
        glb_timespec_add (&wdog->next, wdog->interval);

        int err;
        do {
            err = pthread_cond_timedwait (&wdog->cond, &wdog->lock,
                                          &wdog->next);
        } while (err != ETIMEDOUT && !wdog->quit);

        if (wdog->quit) break;

        wdog_collect_results (wdog);
    }
    wdog->join = true;
    GLB_MUTEX_UNLOCK(&wdog->lock);

    return NULL;
}

static void
wdog_dst_cleanup (glb_wdog_t* wdog)
{
    int i;

    // tell all backend threads to quit
    for (i = 0; i < wdog->n_dst; i++)
    {
        wdog_dst_t* d = &wdog->dst[i];

        glb_log_debug ("Signaling backend thread %u to quit.", d->ctx->id);

        GLB_MUTEX_LOCK (&d->ctx->lock);
        if (!d->ctx->quit)
        {
            d->ctx->quit = true;
            pthread_cond_signal (&d->ctx->cond);
        }
        GLB_MUTEX_UNLOCK (&d->ctx->lock);
    }

    // join the threads and free contexts
    for (i = 0; i < wdog->n_dst; i++)
    {
        wdog_dst_t* d = &wdog->dst[i];
        pthread_join (d->ctx->id, NULL);
        if (d->weight >= 0.0) glb_pool_drop_dst (wdog->pool, &d->dst.addr);
        wdog_dst_free (d);
    }
}

glb_wdog_t*
glb_wdog_create (const glb_cnf_t* cnf, glb_router_t* router, glb_pool_t* pool)
{
    assert (router);
    assert (cnf->watchdog);

    glb_wdog_t* ret = calloc (1, sizeof(*ret));

    if (ret)
    {
        int err = - wdog_backend_factory (cnf, &ret->backend);
        if (err)
        {
            glb_log_error ("Failed to initialize the backend: %d (%s)",
                           err, strerror(err));
            free(ret);
            return NULL;
        }

        assert (ret->backend.thread);
        assert (ret->backend.ctx || !ret->backend.destroy);
        assert (ret->backend.destroy || !ret->backend.ctx);

        ret->cnf    = cnf;
        ret->router = router;
        ret->pool   = pool;
        ret->dst    = NULL;

        pthread_mutex_init (&ret->lock, NULL);
        pthread_cond_init  (&ret->cond, NULL);

        /* making this slightly bigger than backend polling interval
         * to make sure that there is always a ready result for us to collect */
        ret->interval = cnf->interval * 1.1;

        int i;
        for (i = 0; i < cnf->n_dst; i++) {
            if (wdog_change_dst(ret, &cnf->dst[i], true) < 0) {
                wdog_dst_cleanup (ret);
                pthread_cond_destroy  (&ret->cond);
                pthread_mutex_destroy (&ret->lock);
                free (ret->dst);
                free (ret);
                return NULL;
            }
        }

        assert (ret->n_dst == cnf->n_dst);

        GLB_MUTEX_LOCK (&ret->lock);
        pthread_create (&ret->thd, NULL, wdog_main_loop, ret);
        pthread_cond_wait (&ret->cond, &ret->lock);
        GLB_MUTEX_UNLOCK (&ret->lock);
    }

    return ret;
}

void
glb_wdog_destroy(glb_wdog_t* wdog)
{
    GLB_MUTEX_LOCK (&wdog->lock);
    wdog->quit = true;
    pthread_cond_signal (&wdog->cond);
    GLB_MUTEX_UNLOCK (&wdog->lock);
    pthread_join (wdog->thd, NULL);
    wdog_dst_cleanup (wdog);
    pthread_cond_destroy  (&wdog->cond);
    pthread_mutex_destroy (&wdog->lock);
    wdog->backend.destroy (wdog->backend.ctx);
    free (wdog->dst);
    free (wdog);
}


size_t
glb_wdog_print_info (glb_wdog_t* wdog, char* buf, size_t buf_len)
{
    assert (wdog);
    assert (buf);
    assert (buf_len > 128);

    size_t len = 0;
    int    n_dst;
    int    i;

    len += snprintf(buf + len, buf_len - len, "Watchdog:\n"
               "------------------------------------------------------------\n"
               "        Address       : exp  setw     state    lat     curw\n");

    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    GLB_MUTEX_LOCK (&wdog->lock);

    for (i = 0; i < wdog->n_dst; i++)
    {
        wdog_dst_t* d = &wdog->dst[i];
        glb_sockaddr_str_t addr = glb_sockaddr_to_astr (&d->dst.addr);

        len += snprintf (buf + len, buf_len - len,
                         "%s :  %s %7.3f %s  %7.5f %7.3f\n",
                         addr.str,
                         d->explicit ? "+" : " ",
                         d->dst.weight,
                         glb_dst_state_str[d->result.state],
                         d->result.latency,
                         d->weight
            );

        if (len == buf_len) {
            buf[len - 1] = '\0';
            GLB_MUTEX_UNLOCK (&wdog->lock);
            return (len - 1);
        }
    }

    n_dst = wdog->n_dst;

    GLB_MUTEX_UNLOCK (&wdog->lock);

    len += snprintf(buf + len, buf_len - len,
                    "------------------------------------------------------------\n"
                    "Destinations: %d\n", n_dst);

    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    return len;
}
