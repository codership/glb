/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
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

#undef NDEBUG // for now

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

typedef struct wdog_dst
{
    bool                      explicit; //! was added explicitly, never remove
    glb_dst_t                 dst;
    double                    weight;
    glb_wdog_check_t          result;
    bool                      memb_changed;
    glb_backend_thread_ctx_t* ctx;      //! backend thread context
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
    char* addr = strdup (glb_socket_addr_to_string (&dst->addr));

    if (addr)
    {
        char* colon = strchr (addr, ':');

        if (colon) {
            *colon = '\0';
            long port = strtol (colon + 1, NULL, 10);
            assert (port > 0 && port <= 65535);

            glb_backend_thread_ctx_t* ret = calloc (1, sizeof(*ret));

            if (ret) {
                glb_log_debug ("Created context for %s:%ld", addr, port);
                ret->backend = backend;
                pthread_mutex_init (&ret->lock, NULL);
                pthread_cond_init  (&ret->cond, NULL);
                ret->addr = addr;
                ret->port = port;
                ret->interval = interval;
                return ret;
            }
        }

        free (addr);
    }

    return NULL;
}

static void
wdog_backend_thread_ctx_destroy (glb_backend_thread_ctx_t* ctx)
{
    free (ctx->addr);
    free ((void*)ctx->result.others);
    pthread_mutex_destroy (&ctx->lock);
    pthread_cond_destroy  (&ctx->cond);
    free (ctx);
}

int
glb_wdog_change_dst (glb_wdog_t*      const wdog,
                     const glb_dst_t* const dst,
                     bool             const explicit)
{
    int         i;
    void*       tmp;
    wdog_dst_t* d = NULL;

    GLB_MUTEX_LOCK (&wdog->lock);

    // try to find destination in the list
    for (i = 0; i < wdog->n_dst; i++) {
        if (glb_dst_is_equal(&wdog->dst[i].dst, dst)) {
            d = &wdog->dst[i];
            break;
        }
    }

    // sanity check
    if (!d && dst->weight < 0) {
        GLB_MUTEX_UNLOCK (&wdog->lock);
#ifdef GLBD
        char tmp[256];
        glb_dst_print (tmp, sizeof(tmp), dst);
        glb_log_warn ("Command to remove inexisting destination: %s", tmp);
#endif
        return -EADDRNOTAVAIL;
    }

    if (!d) { // add destination

        assert (i == wdog->n_dst);

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
                    d = wdog->dst + wdog->n_dst;
                    wdog->n_dst++;
                    memset (d, 0, sizeof(*d));
                    d->explicit = explicit;
                    d->dst      = *dst;
                    d->ctx      = ctx;
                }
                else {
                    i = -ctx->errn;
                    pthread_join (ctx->id, NULL);
                    wdog_backend_thread_ctx_destroy(ctx);
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
            GLB_MUTEX_LOCK (&d->ctx->lock);
            d->ctx->quit = true;
            pthread_cond_signal (&d->ctx->cond);
            GLB_MUTEX_UNLOCK (&d->ctx->lock);
            /* thread will be joined context will be cleaned up later */
        }
        else
        {
            // no right to remove, just mark it inaccessible
            d->dst.weight = -1.0;
        }
    }
    else if (d->dst.weight != dst->weight) {
        d->dst.weight = dst->weight;
    }

    GLB_MUTEX_UNLOCK (&wdog->lock);

    return i;
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
        glb_log_error("'%s' watchdog not implemented.", cnf->watchdog);
        return -ENOSYS;
    }
}

static inline int
wdog_copy_result (wdog_dst_t* d, double* max_lat)
{
    double old_lat    = d->result.latency;
    char*  others     = d->result.others;
    size_t others_len = d->result.others_len;

    GLB_MUTEX_LOCK (&d->ctx->lock);
    {
        glb_wdog_check_t* res = &d->ctx->result;

        d->result = *res;
        res->ready = false;

        // restore original buffer
        d->result.others     = others;
        d->result.others_len = others_len;

        if (d->result.ready) {
            if (GLB_DST_NOTFOUND == d->result.state) {
                if (!d->explicit) { // if selfdiscovered - schedule for cleanup
                    d->ctx->quit = true;
                    pthread_cond_signal (&d->ctx->cond);
                }
            }
            else { // remote destination is live, handle others string
                bool changed_length = false;

                if (others_len < res->others_len ||
                    others_len > (res->others_len * 2)) {
                    // buffer size is too different, reallocate
                    d->result.others = realloc (others, res->others_len);
                    if (!d->result.others) {
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

                if (res->others_len > 0) {
                    if (d->result.others_len >= res->others_len &&
                        (changed_length ||
                         strcmp(d->result.others, res->others))){
                        d->memb_changed = true;
                        strcpy (d->result.others, res->others);
                    }
                }
            }
        }
    }
    GLB_MUTEX_UNLOCK (&d->ctx->lock);

    if (d->result.ready && GLB_DST_READY == d->result.state) {
        // smooth latency measurement with the previous one
        d->result.latency = (d->result.latency + old_lat * 2.0) / 3.0;
        if (*max_lat < d->result.latency) *max_lat = d->result.latency;
    }
    else {
        // preserve previously measured latency
        d->result.latency = old_lat;
    }

    return 0;
}

// returns latency adjusted weight
static inline double
wdog_result_weight (wdog_dst_t* const d, double const max_lat)
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
        if (max_lat > 0) return d->dst.weight * max_lat / d->result.latency;
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

// collects and processes results, returns the number of results collected
static int
wdog_collect_results (glb_wdog_t* const wdog)
{
#ifdef GLBD
    if (wdog->cnf->verbose) glb_log_debug ("main loop collecting...");
#endif
    double max_lat = 0.0;
    int results = 0;

    int i;
    for (i = 0; i < wdog->n_dst; i++)
    {
        wdog_copy_result (&wdog->dst[i], &max_lat);
    }

    int const old_n_dst = wdog->n_dst;

    for (i = wdog->n_dst - 1; i >= 0; i--) // reverse order for ease of cleanup
    {
        wdog_dst_t* d = &wdog->dst[i];
        double new_weight;

        if (d->ctx->join) {
            pthread_join (d->ctx->id, NULL);
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
            new_weight = wdog_result_weight (d, max_lat);
        }
        else {
            // have heard nothing from the backend thread, put dest on hold
            new_weight = d->weight > 0.0 ? 0.0 : d->weight;
        }

        static double const WEIGHT_TOLERANCE = 0.1; // 10%

        if (new_weight != d->weight &&
            (new_weight <= 0.0 ||
             fabs(d->weight/new_weight - 1.0) > WEIGHT_TOLERANCE)) {
            glb_dst_t dst = d->dst;
            dst.weight = new_weight;
            int ret = glb_router_change_dst (wdog->router, &dst);
            glb_log_debug ("Changing weight %6.3f -> %6.3f:  %d (%s)",
                           d->weight, new_weight,
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
        if (tmp) wdog->dst = tmp;
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
        // try to get at least one destination confirmed
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
        wdog_backend_thread_ctx_destroy (d->ctx);
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

        pthread_mutex_init (&ret->lock, NULL);
        pthread_cond_init  (&ret->cond, NULL);

        /* making this slightly bigger than backend polling interval
         * to make sure that there is always a ready result for us to collect */
        ret->interval = cnf->interval * 1.1;

        int i;
        for (i = 0; i < cnf->n_dst; i++) {
            if (glb_wdog_change_dst(ret, &cnf->dst[i], true) < 0) {
                wdog_dst_cleanup (ret);
                pthread_cond_destroy  (&ret->cond);
                pthread_mutex_destroy (&ret->lock);
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
    pthread_cond_wait (&wdog->cond, &wdog->lock);
    wdog_dst_cleanup (wdog);
    pthread_cond_destroy  (&wdog->cond);
    GLB_MUTEX_UNLOCK (&wdog->lock);
    pthread_mutex_destroy (&wdog->lock);
    int i;
    for (i = 0; i < wdog->n_dst; i++)
    {
        wdog_dst_free (&wdog->dst[i]);
    }
    wdog->backend.destroy (wdog->backend.ctx);
    free (wdog->dst);
    free (wdog);
}


