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
    assert (ctx->host);

    const char* const cmd = ctx->backend->cmd;
    size_t const cmd_len  = strlen(cmd);

    /* we need to insert host:port as a first argument to command */
    size_t const ret_len = cmd_len + strlen(ctx->host) + 7;
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
                                ctx->host, ctx->port);
        memcpy (ret + addr_offset,
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
    size_t const res_size = 4096;
    char*  const res_buf  = malloc (res_size);
    if (!res_buf) {
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

    char* pargv[4] = { strdup ("sh"), strdup ("-c"), strdup (cmd), NULL };

    if (!pargv[0] || !pargv[1] || !pargv[2]) goto cleanup;

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

        ctx->errn = glb_proc_start (&pid, pargv, NULL, &io, NULL);

        if (!ctx->errn)
        {
            assert (io);
            assert (pid);

            char* ret = fgets (res_buf, res_size, io);

            if (ret)
            {
                r.timestamp = glb_time_now();
                r.latency   = glb_time_seconds (r.timestamp - start);

                char* endptr;
                long st = strtol (res_buf, &endptr, 10);
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
                    res_buf[res_size - 1] = '\0';
                    glb_log_error ("Failed to parse process output: '%s'",
                                   res_buf);
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

        /* We want to check working nodes very frequently to learn when they
         * go down. For failed nodes we can make longer intervals to minimize
         * the noise. */
        int interval_mod = r.state > GLB_DST_NOTFOUND ? 1 : 10;

        glb_timespec_add (&next, ctx->interval * interval_mod); // next wakeup

        if (pthread_mutex_lock (&ctx->lock)) abort();

        switch (ctx->waiting)
        {
        case 0:  break;
        case 1:  pthread_cond_signal (&ctx->cond); break;
        default: pthread_cond_broadcast (&ctx->cond);
        }
        ctx->waiting = 0;

        if (ctx->errn) break;

        ctx->result = r;

        if (ETIMEDOUT != pthread_cond_timedwait (&ctx->cond, &ctx->lock, &next))
        {
            /* interrupted by a signal, shift the beginning of next interval */
            next = glb_timespec_now();
        }
    }

    memset (&ctx->result, 0, sizeof(ctx->result));
    ctx->result.state = GLB_DST_NOTFOUND;
    ctx->join = true; /* ready to be joined */

    if (pthread_mutex_unlock(&ctx->lock)) abort();

cleanup:

    free (pargv[2]); free (pargv[1]); free (pargv[0]);
    free (cmd);
    free (res_buf);

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

