/*
 * Copyright (C) 2012-2013 Codership Oy <info@codership.com>
 *
 * A unit to spawn external processes. No human should have ever had to write
 * that, but POSIX committee decided otherwise.
 *
 * Only GLB dependency is logging macros, which can easily be commented out
 * or redefined.
 *
 * Spawning without creating the parent process memory copy (probably the only
 * reason to use this instead of fork()) requires _GNU_SOURCE to be defined.
 *
 * $Id$
 */

#include "glb_proc.h"
#include "glb_log.h"

#include <stdbool.h>  // bool
#include <errno.h>    // errno
#include <assert.h>
#include <string.h>   // strdup()
#include <spawn.h>    // posix_spawn()
#include <unistd.h>   // pipe()
#include <fcntl.h>    // fcntl()
#include <sys/wait.h> // waitpid()

/*! POSIX_SPAWN_USEVFORK requires _GNU_SOURCE */
#ifndef POSIX_SPAWN_USEVFORK
# define POSIX_SPAWN_USEVFORK 0
#endif

extern char** environ; // environment variables for proc_run

static int const PIPE_READ  = 0;
static int const PIPE_WRITE = 1;
static int const STDIN_FD   = 0;
static int const STDOUT_FD  = 1;
static int const STDERR_FD  = 2;

static inline bool
proc_need_to_close_fd (int fd)
{
    int flags = fcntl(fd, F_GETFD);
    return (flags >= 0 /* && !(flags & FD_CLOEXEC) */);
    /* FDs that have FD_CLOEXEC flag set will be closed only AFTER actions
     * specified by posix_spawn_file_actions_addclose() are applied. So if we
     * need to dup2 an FD, it has to be first closed explicitly. */
}

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

    int err = 0;

    // close child's fd - it will be open there if it is open here
    if (proc_need_to_close_fd (child_fd))
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
 * @arg argv
 *      null-terminated process arguments vector, should start with process
 *      image and then each argument in a separate string.
 *      (the same thing that is passed as a second argument to main())
 * @arg envp
 *      null-terminated vector of environment variables to be used by the
 *      spawned process, if NULL, extern char **environ (parent environment)
 *      will be used.
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
int glb_proc_start (pid_t* pid, char* pargv[], char* envp[],
                    FILE** std_in, FILE** std_out, FILE** std_err)
{
    int err, err1;
    int stdin_pipe[2] = { -1, -1 };
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };

    *pid = 0;

    posix_spawnattr_t attr;
    err = posix_spawnattr_init (&attr);
    if (err)
    {
        glb_log_error ("posix_spawnattr_init() failed: %d (%s)",
                       err, strerror(err));
        return err;
    }

    err = posix_spawnattr_setflags (&attr,
//                                    POSIX_SPAWN_SETPGROUP |
//                                    POSIX_SPAWN_SETSIGDEF |
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

    if (std_in &&
        (err = proc_pipe_fds(&fact, stdin_pipe, STDIN_FD, false, std_in)))
    {
        glb_log_error ("Failed to associate a stream with child's stdin: "
                       "%d (%s)", err, strerror(err));
        goto cleanup_fact;
    }

    if (std_out &&
        (err = proc_pipe_fds(&fact, stdout_pipe, STDOUT_FD, true, std_out)))
    {
        glb_log_error ("Failed to associate a stream with child's stdout: "
                       "%d (%s)", err, strerror(err));
        goto cleanup_stdin;
    }

    if (std_err &&
        (err = proc_pipe_fds(&fact, stderr_pipe, STDERR_FD, true, std_err)))
    {
        glb_log_error ("Failed to associate a stream with child's stderr: "
                       "%d (%s)", err, strerror(err));
        goto cleanup_stdout;
    }

    /* Finally... */
    err = posix_spawnp (pid, pargv[0], &fact, &attr, pargv, envp?envp:environ);

    if (err)
    {
        glb_log_error ("posix_spawnp('%s') failed: %d (%s)",
                       pargv[0], err, strerror(err));
        *pid = 0; // just to make sure it was not messed up in the call

        if (std_err) {
        if (NULL != *std_err) { fclose (*std_err); *std_err = NULL; }
        else if (stderr_pipe[PIPE_READ] >= 0) close (stderr_pipe[PIPE_READ]);
        }

cleanup_stdout:
        if (std_out) {
        if (NULL != *std_out) { fclose (*std_out); *std_out = NULL; }
        else if (stdout_pipe[PIPE_READ] >= 0) close (stdout_pipe[PIPE_READ]);
        }

cleanup_stdin:
        if (std_in) {
        if (NULL != *std_in) { fclose (*std_in); *std_in = NULL; }
        else if (stdin_pipe[PIPE_WRITE] >= 0) close (stdin_pipe[PIPE_WRITE]);
        }
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
int glb_proc_startc (pid_t* pid, const char* cmd, char* envp[],
                     FILE** std_in, FILE** std_out, FILE** std_err)
{
    char* pargv[4] = { strdup ("sh"), strdup ("-c"), strdup (cmd), NULL };

    int err;

    if (pargv[0] && pargv[1] && pargv[2])
        err = glb_proc_start (pid, pargv, envp, std_in, std_out, std_err);
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

