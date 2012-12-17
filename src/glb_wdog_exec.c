/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * This is backend that polls destinations by running external scripts or
 * programs.
 *
 * $Id$
 */

#include "glb_wdog_exec.h"

#include <stdlib.h>   // calloc()/free()/abort()
#include <string.h>   // strdup()
#include <errno.h>    // ENOMEM
#include <sys/time.h> // gettimeofday()


struct glb_backend_ctx
{
    const char* spec; // just for example
};

static void
backend_exec_destroy (glb_backend_ctx_t* ctx)
{
    free ((void*)ctx->spec);
    free (ctx);
}

static void*
backend_exec_thread (void* arg)
{
    glb_backend_thread_ctx_t* ctx = arg;

    struct timespec next = glb_timespec_now(); /* establish starting point */

    /* failure to lock/unlock the mutex is absolutely fatal */
    if (pthread_mutex_lock (&ctx->lock)) abort();

    pthread_cond_signal (&ctx->cond); // watchdog is waiting for this signal

    while (!ctx->quit) /* main loop */
    {
#if SKIP
        if (pthread_mutex_unlock (&ctx->lock)) abort();

        /* here we should be polling the destination - backend-specific part */

        if (pthread_mutex_lock (&ctx->lock)) abort();
#endif

        ctx->result.state   = GLB_DST_READY;// destination always ready
        ctx->result.latency = 1.0;          // same latency for all destinations
        ctx->result.others  = NULL;         // no auto-discovered destinations
        ctx->result.others_len = 0;
        ctx->result.ready   = true;         // new data ready

        glb_timespec_add (&next, ctx->interval); // next wakeup

        /* this unlocks the context and watchdog can have full access to it */
        pthread_cond_timedwait (&ctx->cond, &ctx->lock, &next);
        /* here the context is locked again */
    }

    ctx->join = true; /* ready to be joined */

    if (pthread_mutex_unlock(&ctx->lock)) abort();

    return NULL;
}

static int
backend_exec_init (glb_backend_t* backend, const char* spec)
{
    glb_backend_ctx_t* ctx = calloc (1, sizeof(*ctx));

    if (!ctx) return -ENOMEM;

    if (spec) {
        ctx->spec = strdup(spec);
        if (!ctx->spec) {
            free (ctx);
            return -ENOMEM;
        }
    }
    else {
        ctx->spec = NULL;
    }

    backend->ctx     = ctx;
    backend->thread  = backend_exec_thread;
    backend->destroy = backend_exec_destroy;

    return 0;
}

glb_backend_init_t glb_backend_exec_init = backend_exec_init;

