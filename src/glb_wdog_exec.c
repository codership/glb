/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * This is backend that polls destinations by running external scripts or
 * programs.
 *
 * $Id$
 */

#include "glb_wdog_exec.h"
#include "glb_proc.h"
#include "glb_log.h"

#include <stdlib.h>   // calloc()/free()/abort()
#include <string.h>   // strdup()
#include <errno.h>    // ENOMEM
#include <sys/time.h> // gettimeofday()
#include <stddef.h>   // ptrdiff_t
#include <ctype.h>    // isspace()

struct glb_backend_ctx
{
    const char* cmd;
};

static void
exec_destroy (glb_backend_ctx_t* ctx)
{
    free ((void*)ctx->cmd);
    free (ctx);
}

static char*
exec_create_cmd (const glb_backend_thread_ctx_t* ctx)
{
    assert (ctx);
    assert (ctx->backend);
    assert (ctx->backend->cmd);
    assert (ctx->addr);

    const char* const cmd = ctx->backend->cmd;
    size_t const cmd_len  = strlen(cmd);

    /* we need to insert address:port as a first argument to command */
    size_t const ret_len = cmd_len + strlen(ctx->addr) + 7;
    char* ret = malloc (ret_len);

    if (ret)
    {
        char* first_space = strchr (cmd, ' ');
        ptrdiff_t cmd_offset;

        if (first_space)
            cmd_offset = first_space - cmd;
        else
            cmd_offset = cmd_len;

        memcpy (ret, cmd, cmd_offset);
        ptrdiff_t addr_offset = cmd_offset;
        addr_offset += sprintf (ret + cmd_offset, " %s:%hu",
                                ctx->addr, ctx->port);
        memcpy (ret  + addr_offset,
                cmd + cmd_offset,
                cmd_len - cmd_offset + 1); // plus trailing '\0'
    }

    return ret;
}

static void*
exec_thread (void* arg)
{
    glb_backend_thread_ctx_t* ctx = arg;

    char* cmd = NULL;

    /* read buffer */
    int const res_size = 4096;
    char* const res = malloc (res_size);
    if (!res) {
        ctx->errn = ENOMEM;
        pthread_cond_signal (&ctx->cond);
        goto cleanup;
    }

    cmd = exec_create_cmd (ctx);
    if (!cmd) {
        ctx->errn = ENOMEM;
        pthread_cond_signal (&ctx->cond);
        goto cleanup;
    }

    glb_log_debug ("exec thread %lld, cmd: '%s'", ctx->id, cmd);

    struct timespec next = glb_timespec_now(); /* establish starting point */

    /* failure to lock/unlock the mutex is absolutely fatal */
    if (pthread_mutex_lock (&ctx->lock)) abort();

    pthread_cond_signal (&ctx->cond);

    while (!ctx->quit) /* MAIN LOOP */
    {
        if (pthread_mutex_unlock (&ctx->lock)) abort();

        struct glb_wdog_check r = { 0, 0, 0, 0, 0 };
        pid_t pid;
        FILE* io;

        glb_time_t start = glb_time_now();

        ctx->errn = glb_proc_startc (&pid, cmd, NULL, &io, NULL);

        if (!ctx->errn)
        {
            assert (io);
            assert (pid);

            char* ret = fgets (res, res_size, io);

            if (ret)
            {
                r.latency = glb_time_seconds (glb_time_now() - start);

                char* endptr;
                long long st = strtoll (res, &endptr, 10);
                if (('\0' == endptr[0] || isspace(endptr[0])) &&
                    st >= GLB_DST_NOTFOUND && st <= GLB_DST_READY)
                {
                    r.state = st;
                    if ('\0' != endptr[0]) {
                        endptr++;
                        r.others_len = strlen (endptr);
                        if (r.others_len > 0) r.others = endptr;
                    }
                    r.ready = true;
                }
                else {
                    ctx->errn = EPROTO;
                    glb_log_error ("Failed to parse process output: '%s'", res);
                }
            }
            else {
                ctx->errn = errno;
                glb_log_error ("Failed to read process output: %d (%s)",
                               errno, strerror(errno));
            }
        }

        ctx->errn = glb_proc_end(pid);
        if (io) fclose (io);

        if (pthread_mutex_lock (&ctx->lock)) abort();

        if (ctx->errn) break;

        ctx->result = r;

        /* We want to check working nodes very frequently to learn when they
         * go down. For failed nodes we can make longer intervals to minimize
         * the noise. */
        int interval_mod = r.state > GLB_DST_NOTFOUND ? 1 : 10;

        glb_timespec_add (&next, ctx->interval * interval_mod); // next wakeup

        pthread_cond_timedwait (&ctx->cond, &ctx->lock, &next);
    }

cleanup:

    free (cmd);
    free (res);

    memset (&ctx->result, 0, sizeof(ctx->result));

    ctx->join = true; /* ready to be joined */

    if (pthread_mutex_unlock(&ctx->lock)) abort();

    return NULL;
}

static int
exec_init (glb_backend_t* backend, const char* spec)
{
    if (!spec || strlen(spec) == 0) {
        glb_log_error ("'exec' backend requires non-empty command line.");
        return -EINVAL;
    }

    glb_backend_ctx_t* ctx = calloc (1, sizeof(*ctx));

    if (!ctx) return -ENOMEM;

    ctx->cmd = strdup(spec);
    if (!ctx->cmd) {
        free (ctx);
        return -ENOMEM;
    }

    backend->ctx     = ctx;
    backend->thread  = exec_thread;
    backend->destroy = exec_destroy;

    return 0;
}

glb_backend_init_t glb_backend_exec_init = exec_init;

