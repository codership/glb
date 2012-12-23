/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * A unit to spawn external processes. No human should have ever had to write
 * that, but POSIX committee decided otherwise.
 *
 * Only GLB dependency is logging macros, which can easily be commented out
 * or redefined.
 *
 * $Id$
 */

#include "glb_proc.h"
#include "glb_log.h"

/* for process management */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE // POSIX_SPAWN_USEVFORK flag
#endif

#include <stdbool.h>  // bool
#include <errno.h>    // errno
#include <assert.h>
#include <string.h>   // strdup()
#include <spawn.h>    // posix_spawn()
#include <unistd.h>   // pipe()
#include <sys/wait.h> // waitpid()

extern char** environ; // environment variables for proc_run

#ifndef POSIX_SPAWN_USEVFORK
# define POSIX_SPAWN_USEVFORK 0
#endif

static int const PIPE_READ  = 0;
static int const PIPE_WRITE = 1;
static int const STDIN_FD   = 0;
static int const STDOUT_FD  = 1;
static int const STDERR_FD  = 2;

/* pipe child file descriptor to parent's and associate a stream with it */
static int
proc_pipe_fds (posix_spawn_file_actions_t* fact,
               int* pipe_fds/*[2]*/, int child_fd,
               bool read /* will parent read or write to file descriptor? */,
               FILE** stream)
{
    assert (fact);
    assert (pipe_fds);
    assert (stream);

    int err;

    // close child's fd
    err = posix_spawn_file_actions_addclose (fact, child_fd);
    if (err)
    {
        glb_log_error ("posix_spawn_file_actions_addclose() failed: %d (%s)",
                       err, strerror(err));
        return err;
    }

    if (pipe(pipe_fds))
    {
        err = errno;
        glb_log_error ("pipe() failed: %d (%s)", err, strerror(err));
        return err;
    }

//    glb_log_debug ("Opened pipe: %d %d", pipe_fds[0], pipe_fds[1]);

    if (read)
        *stream = fdopen (pipe_fds[PIPE_READ], "r");
    else
        *stream = fdopen (pipe_fds[PIPE_WRITE], "w");

    if (NULL == *stream) goto cleanup;

    // duplicate our pipe descriptor in place of the closed one
    int const child_end  = read ? PIPE_WRITE : PIPE_READ;
    err = posix_spawn_file_actions_adddup2 (fact,
                                            pipe_fds[child_end], child_fd);
    if (err)
    {
        glb_log_error ("posix_spawn_file_actions_addup2() failed: %d (%s)",
                       err, strerror(err));
        goto cleanup;
    }

    return err;

cleanup:

    close (pipe_fds[0]); pipe_fds[0] = -1;
    close (pipe_fds[1]); pipe_fds[1] = -1;

    if (*stream)
    {
        fclose (*stream);
        *stream = NULL;
    }

    return err;
}


/*!
 * @arg pid
 *      spawned process ID
 * @arg pargv
 *      process arguments vector, should start with process image and then
 *      each argument in a separate string.
 *      (the same thing that is passed as a second argument to main())
 * @arg std_in
 *      Pointer to stream associated with the process standard input (for the
 *      parent to write to). If NULL, the process will inherit stdin from the
 *      parent.
 * @arg std_out
 *      Pointer to stream associated with the process standard output (for the
 *      caller to read from). If NULL, the process will inherit stdout from the
 *      caller.
 * @arg std_err
 *      Pointer to stream associated with the process standard error (for the
 *      parent to read from). If NULL, the process will inherit stderr from the
 *      caller.
 * @return 0 on success or error code.
 */
int glb_proc_start (pid_t* pid, char* pargv[],
                    FILE** std_in, FILE** std_out, FILE** std_err)
{
    int err, err1;

    *pid = 0;

    posix_spawnattr_t attr;
    err = posix_spawnattr_init (&attr);
    if (err)
    {
        glb_log_error ("posix_spawnattr_init() failed: %d (%s)",
                       err, strerror(err));
        return err;
    }

    err = posix_spawnattr_setflags (&attr, POSIX_SPAWN_SETSIGDEF |
                                           POSIX_SPAWN_USEVFORK);
    if (err)
    {
        glb_log_error ("posix_spawnattr_setflags() failed: %d (%s)",
                       err, strerror(err));
        goto cleanup_attr;
    }

    posix_spawn_file_actions_t fact;
    err = posix_spawn_file_actions_init (&fact);
    if (err)
    {
        glb_log_error ("posix_spawn_file_actions_init() failed: %d (%s)",
                       err, strerror(err));
        goto cleanup_attr;
    }

    int stdin_pipe[2] = { -1, -1 };
    if (std_in &&
        (err = proc_pipe_fds(&fact, stdin_pipe, STDIN_FD, false, std_in)))
    {
        glb_log_error ("Failed to associate a stream with child's stdin: "
                       "%d (%s)", err, strerror(err));
        goto cleanup_fact;
    }

    int stdout_pipe[2] = { -1, -1 };
    if (std_out &&
        (err = proc_pipe_fds(&fact, stdout_pipe, STDOUT_FD, true, std_out)))
    {
        glb_log_error ("Failed to associate a stream with child's stdout: "
                       "%d (%s)", err, strerror(err));
        goto cleanup_stdin;
    }

    int stderr_pipe[2] = { -1, -1 };
    if (std_err &&
        (err = proc_pipe_fds(&fact, stderr_pipe, STDERR_FD, true, std_err)))
    {
        glb_log_error ("Failed to associate a stream with child's stderr: "
                       "%d (%s)", err, strerror(err));
        goto cleanup_stdout;
    }

    /* Finally... */
    err = posix_spawnp (pid, pargv[0], &fact, &attr, pargv, environ);

    if (err)
    {
        glb_log_error ("posix_spawnp('%s') failed: %d (%s)",
                       pargv[0], err, strerror(err));
        *pid = 0; // just to make sure it was not messed up in the call

        if (NULL != *std_err) { fclose (*std_err); *std_err = NULL; }
        else if (stderr_pipe[PIPE_READ] >= 0) close (stderr_pipe[PIPE_READ]);

cleanup_stdout:
        if (NULL != *std_out) { fclose (*std_out); *std_out = NULL; }
        else if (stdout_pipe[PIPE_READ] >= 0) close (stdout_pipe[PIPE_READ]);

cleanup_stdin:
        if (NULL != *std_in) { fclose (*std_in); *std_in = NULL; }
        else if (stdin_pipe[PIPE_WRITE] >= 0) close (stdin_pipe[PIPE_WRITE]);
    }

    /* Now we should close child's end of the pipe in the parent */
    if (stdin_pipe [PIPE_READ]  >= 0) close (stdin_pipe [PIPE_READ]);
    if (stdout_pipe[PIPE_WRITE] >= 0) close (stdout_pipe[PIPE_WRITE]);
    if (stderr_pipe[PIPE_WRITE] >= 0) close (stderr_pipe[PIPE_WRITE]);

cleanup_fact:
    err1 = posix_spawn_file_actions_destroy (&fact);
    if (err1)
    {
        glb_log_error ("posix_spawn_file_actions_destroy() failed: %d (%s)\n",
                       err, strerror(err));
    }

cleanup_attr:
    err1 = posix_spawnattr_destroy (&attr);
    if (err1)
    {
        glb_log_error ("posix_spawnattr_destroy() failed: %d (%s)",
                       err, strerror(err));
    }

    return err;
}

/*! Same as glb_proc_start, but spawns "sh -c 'cmd'" */
int glb_proc_startc (pid_t* pid, const char* cmd,
                     FILE** std_in, FILE** std_out, FILE** std_err)
{
    char* pargv[4] = { strdup ("sh"), strdup ("-c"), strdup (cmd), NULL };

    int err;

    if (pargv[0] && pargv[1] && pargv[2])
        err = glb_proc_start (pid, pargv, std_in, std_out, std_err);
    else
        err = ENOMEM;

    free (pargv[2]); free (pargv[1]); free (pargv[0]);

    return err;
}

/*! wait for the process to end */
int glb_proc_end (pid_t pid)
{
    int err;

    if (pid)
    {
        int status;
        if (-1 == waitpid(pid, &status, 0))
        {
            err = errno; assert (err);
            glb_log_error("Waiting for process failed: PID(%ld): %d (%s)",
                          (long)pid, err, strerror (err));
        }
        else
        {                // command completed, check exit status
            if (WIFEXITED (status)) {
                err = WEXITSTATUS (status);
            }
            else {       // command didn't complete with exit()
                glb_log_error("Process was aborted.");
                err = errno ? errno : ECANCELED;
            }

            if (err) {
                switch (err) /* Translate error codes to more meaningful */
                {
                case 126: err = EACCES; break; /* Permission denied */
                case 127: err = ENOENT; break; /* No such file or directory */
                }

                glb_log_error("Process %ld completed with error: %d (%s)",
                              pid, err, strerror(err));
            }
        }
    }
    else
    {
        glb_log_error("Command did not run.");
        err = ESRCH;
    }

    return err;
}

#if REMOVE
static void
proc_close_stream
/*! terminates the process and cleans up */
void glb_proc_end (pid_t pid, FILE* std_in, FILE* std_out, FILE* std_err)
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
#endif

