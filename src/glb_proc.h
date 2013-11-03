/*
 * Copyright (C) 2012-2013 Codership Oy <info@codership.com>
 *
 * $Id: glb_proc.h 160 2013-11-03 14:49:02Z alex $
 */

#ifndef _glb_proc_h_
#define _glb_proc_h_

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>

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
extern int
glb_proc_start (pid_t* pid, char* argv[], char* envp[],
                FILE** std_in, FILE** std_out, FILE** std_err);

/*! Same as glb_proc_start, but spawns "sh -c 'cmd'" so that shell does the
 *  proper command line parsing. */
extern int
glb_proc_startc (pid_t* pid, const char* cmd, char* envp[],
                 FILE** std_in, FILE** std_out, FILE** std_err);

/*! Waits for the process to end, returns exit status */
int glb_proc_end (pid_t pid);

#endif // _glb_proc_h_
