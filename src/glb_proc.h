/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_proc_h_
#define _glb_proc_h_

#include <stdlib.h>
#include <stdio.h>

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
extern int
glb_proc_start (pid_t* pid, char* pargv[],
                FILE** std_in, FILE** std_out, FILE** std_err);

/*! Same as glb_proc_start, but spawns "sh -c 'cmd'" */
extern int
glb_proc_startc (pid_t* pid, const char* cmd,
                 FILE** std_in, FILE** std_out, FILE** std_err);

/*! Waits for the process to end, returns exit status */
int glb_proc_end (pid_t pid);

#endif // _glb_proc_h_
