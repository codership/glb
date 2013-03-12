/*
 * Copyright (C) 2012-2013 Codership Oy <info@codership.com>
 *
 * This is backend that polls destinations by running external scripts or
 * programs.
 *
 * $Id$
 */

#include "glb_wdog_exec.h"
#include "glb_proc.h"
#include "glb_log.h"
#if GLBD
#include "glb_signal.h"
#else
bool const glb_terminate = false;
#endif /* GLBD */

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

static int
exec_send_cmd (const char* cmd, FILE* stream)
{
    if (EOF != fputs (cmd, stream))
    {
        fflush (stream);
        return 0;
    }

    return EIO;
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
        goto init_error;
    }

    cmd = exec_create_cmd (ctx);
    if (!cmd) {
        ctx->errn = ENOMEM;
        pthread_cond_signal (&ctx->cond);
        goto init_error;
    }

    char* pargv[4] = { strdup ("sh"), strdup ("-c"), strdup (cmd), NULL };

    if (!pargv[0] || !pargv[1] || !pargv[2]) {
        ctx->errn = ENOMEM;
        goto init_error;
    }

    pid_t pid     = -1;
    FILE* std_in  = NULL;
    FILE* std_out = NULL;

    ctx->errn = glb_proc_start (&pid, pargv, NULL, &std_in, &std_out, NULL);

init_error:

    glb_log_debug ("exec thread: %lld, errno: %d (%s), pid: %lld, cmd: '%s'",
                   ctx->id, ctx->errn, strerror(ctx->errn),(long long)pid, cmd);

    /* this will skip main loop and fall through to cleanup */
    if (ctx->errn) ctx->quit = true;

    struct timespec next = glb_timespec_now(); /* establish starting point */

    /* failure to lock/unlock the mutex is absolutely fatal */
    if (pthread_mutex_lock (&ctx->lock)) abort();

    pthread_cond_signal (&ctx->cond);

    while (!ctx->quit) /* MAIN LOOP */
    {
        if (pthread_mutex_unlock (&ctx->lock)) abort();

        struct glb_wdog_check r = { 0, 0, 0, 0, 0 };

        glb_time_t start = glb_time_now();

        assert (std_in);

        ctx->errn = exec_send_cmd ("poll\n", std_in);

        if (!ctx->errn)
        {
            assert (std_out);

            char* ret = fgets (res_buf, res_size, std_out);

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
            else if (!glb_terminate) {
                ctx->errn = errno;
                glb_log_error ("Failed to read process output: %d (%s)",
                               errno, strerror(errno));
            }
        }
        else {
            glb_log_error ("Failed to send 'poll' cmd to script: %d (%s)",
                           ctx->errn, strerror (ctx->errn));
        }

        /* We want to check working nodes very frequently to learn when they
         * go down. For failed nodes we can make longer intervals to minimize
         * the noise. */
        int interval_mod = r.state > GLB_DST_NOTFOUND ? 1 : 10;

        glb_timespec_add (&next, ctx->interval * interval_mod); // next wakeup

        if (pthread_mutex_lock (&ctx->lock)) abort();

        switch (ctx->waiting)
        {
        case 0:                                       break;
        case 1:  pthread_cond_signal    (&ctx->cond); break;
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
    } /* MAIN LOOP */

    glb_log_debug ("Watchdog thread for '%s:%hu' exiting: %d (%s)",
                   ctx->host, ctx->port, ctx->errn, strerror(ctx->errn));

    memset (&ctx->result, 0, sizeof(ctx->result));
    ctx->result.state = GLB_DST_NOTFOUND;
    ctx->join = true; /* ready to be joined */

    if (pthread_mutex_unlock(&ctx->lock)) abort();

//cleanup:

    if (pid > 0 && !glb_terminate) {
        assert(std_in);
        assert(std_out);

        int err = exec_send_cmd ("quit\n", std_in);
        if (err) {
            glb_log_error ("Failed to send 'quit' to the process");
        }

        err = glb_proc_end(pid);
        if (err) {
            glb_log_error ("Failed to end process %lld: %d (%s)",
                           (long long)pid, err, strerror(err));
        }

    }

    if (std_in)  fclose (std_in);
    if (std_out) fclose (std_out);

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

