/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * This is backend that polls destinations by running external scripts or
 * programs.
 *
 * TODO: factor out process management routines into separate unit.
 *
 * $Id$
 */

#include "glb_wdog_exec.h"
#include "glb_log.h"

#include <stdlib.h>   // calloc()/free()/abort()
#include <string.h>   // strdup()
#include <errno.h>    // ENOMEM
#include <sys/time.h> // gettimeofday()
#include <stddef.h>   // ptrdiff_t
#include <ctype.h>    // isspace()

/* for process management */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE // POSIX_SPAWN_USEVFORK flag
#endif

#include <spawn.h>    // posix_spawn()
#include <unistd.h>   // pipe()
#include <sys/wait.h> // waitpid()
#include <stdio.h>    // FILE*, fdopen()

#ifndef POSIX_SPAWN_USEVFORK
# define POSIX_SPAWN_USEVFORK 0
#endif

#define PIPE_READ  0
#define PIPE_WRITE 1
#define STDIN_FD   0
#define STDOUT_FD  1

/*! a context for running an external process */
struct proc
{
    const char* str_;
    FILE*       io_;
    int         err_;
    pid_t       pid_;
};

extern char** environ; // environment variables for proc_run

/*!
 * @arg proc
 *      contains the running process context
 * @arg cmd
 *      command to run
 * @arg type
 *      a pointer to a null-terminated string which  must  contain
 *      either  the  letter  'r'  for  reading  or the letter 'w' for writing.
 */
int proc_run (struct proc* const proc,
              const  char* const cmd,
              const  char* const type)
{
    memset (proc, 0, sizeof(*proc));

    proc->err_ = EINVAL;

    if (NULL == cmd || 0 == strlen(cmd))
    {
        glb_log_error ("Can't start a process: null or empty command line.");
        return proc->err_;
    }

    if (NULL == type || (strcmp (type, "w") && strcmp(type, "r")))
    {
        glb_log_error ("type argument should be either \"r\" or \"w\".");
        return proc->err_;
    }

    proc->str_ = strdup(cmd);
    if (0 == proc->str_)
    {
        glb_log_error ("Can't allocate command line of size: %zu", strlen(cmd));
        proc->err_ = ENOMEM;
        return proc->err_;
    }

    int pipe_fds[2] = { -1, };
    if (pipe(pipe_fds))
    {
        proc->err_ = errno;
        glb_log_error ("pipe() failed: %d (%s)",
                       proc->err_, strerror(proc->err_));
        /* we don't worry about str_ cleanup here - proc_cleanup() will do it */
        return proc->err_;
    }

    // which end of pipe will be returned to parent
    int const parent_end = strcmp(type,"w") ? PIPE_READ : PIPE_WRITE;
    int const child_end  = parent_end == PIPE_READ ? PIPE_WRITE : PIPE_READ;
    int const close_fd   = parent_end == PIPE_READ ? STDOUT_FD  : STDIN_FD;

    char* const pargv[4] =
        {
            strdup("sh"),
            strdup("-c"),
            strdup(proc->str_),
            NULL
        };

    if (!(pargv[0] && pargv[1] && pargv[2]))
    {
        proc->err_ = ENOMEM;
        glb_log_error ("Failed to allocate pargv[] array.");
        goto cleanup_pipe;
    }

    posix_spawnattr_t attr;
    proc->err_ = posix_spawnattr_init (&attr);
    if (proc->err_)
    {
        glb_log_error ("posix_spawnattr_init() failed: %d (%s)",
                       proc->err_, strerror(proc->err_));
        goto cleanup_pipe;
    }

    proc->err_ = posix_spawnattr_setflags (&attr, POSIX_SPAWN_SETSIGDEF |
                                                  POSIX_SPAWN_USEVFORK);
    if (proc->err_)
    {
        glb_log_error ("posix_spawnattr_setflags() failed: %d (%s)",
                       proc->err_, strerror(proc->err_));
        goto cleanup_attr;
    }

    posix_spawn_file_actions_t fact;
    proc->err_ = posix_spawn_file_actions_init (&fact);
    if (proc->err_)
    {
        glb_log_error ("posix_spawn_file_actions_init() failed: %d (%s)",
                       proc->err_, strerror(proc->err_));
        goto cleanup_attr;
    }

    // close child's stdout|stdin depending on what we returning
    proc->err_ = posix_spawn_file_actions_addclose (&fact, close_fd);
    if (proc->err_)
    {
        glb_log_error ("posix_spawn_file_actions_addclose() failed: %d (%s)",
                       proc->err_, strerror(proc->err_));
        goto cleanup_fact;
    }

    // substitute our pipe descriptor in place of the closed one
    proc->err_ = posix_spawn_file_actions_adddup2 (&fact,
                                             pipe_fds[child_end], close_fd);
    if (proc->err_)
    {
        glb_log_error ("posix_spawn_file_actions_addup2() failed: %d (%s)",
                       proc->err_, strerror(proc->err_));
        goto cleanup_fact;
    }

    proc->err_ = posix_spawnp (&proc->pid_, pargv[0], &fact, &attr, pargv,
                               environ);
    if (proc->err_)
    {
        glb_log_error ("posix_spawnp(%s) failed: %d (%s)",
                       pargv[2], proc->err_, strerror(proc->err_));
        proc->pid_ = 0; // just to make sure it was not messed up in the call
        goto cleanup_fact;
    }

    proc->io_ = fdopen (pipe_fds[parent_end], type);

    if (proc->io_)
    {
        pipe_fds[parent_end] = -1; // skip close on cleanup
    }
    else
    {
        proc->err_ = errno;
        glb_log_error ("fdopen() failed: %d (%s)",
                       proc->err_, strerror(proc->err_));
    }

    int err; // to preserve err_ code

cleanup_fact:
    err = posix_spawn_file_actions_destroy (&fact);
    if (err)
    {
        glb_log_error ("posix_spawn_file_actions_destroy() failed: %d (%s)\n",
                       err, strerror(err));
    }

cleanup_attr:
    err = posix_spawnattr_destroy (&attr);
    if (err)
    {
        glb_log_error ("posix_spawnattr_destroy() failed: %d (%s)",
                       err, strerror(err));
    }

cleanup_pipe:
    if (pipe_fds[0] >= 0) close (pipe_fds[0]);
    if (pipe_fds[1] >= 0) close (pipe_fds[1]);

    free (pargv[0]);
    free (pargv[1]);
    free (pargv[2]);

    return proc->err_;
}

/*! terminated the process and cleanup */
void proc_cleanup (struct proc* proc)
{
    if (proc->io_)
    {
        assert (proc->pid_);
        assert (proc->str_);

        glb_log_warn("Closing pipe to child process: %s, PID(%ld) "
                     "which might still be running.",
                     proc->str_, (long)proc->pid_);

        if (fclose (proc->io_) == -1)
        {
            proc->err_ = errno;
            glb_log_error("fclose() failed: %d (%s)",
                          proc->err_, strerror(proc->err_));
        }
    }

    if (proc->str_) free ((void*)proc->str_);
}

/*! wait for the process to end */
int proc_wait (struct proc* proc)
{
  if (proc->pid_)
  {
      int status;
      if (-1 == waitpid(proc->pid_, &status, 0))
      {
          proc->err_ = errno; assert (proc->err_);
          glb_log_error("Waiting for process failed: %s, PID(%ld): %d (%s)",
                        proc->str_, (long)proc->pid_, proc->err_,
                        strerror (proc->err_));
      }
      else
      {                // command completed, check exit status
          if (WIFEXITED (status)) {
              proc->err_ = WEXITSTATUS (status);
          }
          else {       // command didn't complete with exit()
              glb_log_error("Process was aborted.");
//              proc->err_ = errno ? errno : ECHILD;
          }

          if (proc->err_) {
              switch (proc->err_) /* Translate error codes to more meaningful */
              {
              case 126: proc->err_ = EACCES; break; /* Permission denied */
              case 127: proc->err_ = ENOENT; break; /* No such file or directory */
              }
              glb_log_error("Process completed with error: %s: %d (%s)",
                            proc->str_, proc->err_, strerror(proc->err_));
          }

          proc->pid_ = 0;
          if (proc->io_) fclose (proc->io_);
          proc->io_ = NULL;
      }
  }
  else {
      assert (NULL == proc->io_);
      glb_log_error("Command did not run: %s", proc->str_);
  }

  return proc->err_;
}


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
//        char*  others = NULL;
//        size_t others_len = 0;
//        glb_dst_state_t state = GLB_DST_NOTFOUND;
        struct proc p;

        glb_time_t start = glb_time_now();

        ctx->errn = proc_run (&p, cmd, "r");

        if (!ctx->errn) {
            char* ret = fgets (res, res_size, p.io_);
            if (ret) {
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

        proc_wait(&p);
        proc_cleanup(&p);

        r.latency = glb_time_seconds (glb_time_now() - start);

        if (p.err_) ctx->errn = p.err_;

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

