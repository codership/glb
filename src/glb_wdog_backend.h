/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * Declarations of watchdog backend interface.
 * See example dummy inplementation in glb_wdog_backend.c
 *
 * $Id$
 */

#ifndef _glb_wdog_backend_h_
#define _glb_wdog_backend_h_

#include "glb_time.h"   // for time manipulation functions and glb_time_t
#include "glb_signal.h" // for extern volatile sig_atomic_t glb_terminate;

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>


/*! This is an opaque global backend context created by glb_backend_init_t */
typedef struct glb_backend_ctx glb_backend_ctx_t;


/*! Release global backend resources after joining all backend threads. */
typedef void (*glb_backend_destroy_t) (glb_backend_ctx_t* backend);


typedef enum glb_dst_state
{
    GLB_DST_NOTFOUND = 0, //! destination not reachable (probably dead)
    GLB_DST_NOTREADY,     //! destination not ready to accept connections
    GLB_DST_AVOID,        //! destination better be avoided (overloaded/blocked)
    GLB_DST_READY         //! destination fully functional
} glb_dst_state_t;


typedef struct glb_wdog_check
{
    glb_dst_state_t state;    //! observed state of the destination
    glb_time_t      timestamp;//! result timestamp
    double          latency;  //! communication latency (seconds)
    char*           others;   //! other cluster memebers if any in the usual fmt
    size_t          others_len; //! length of others buffer, not actual string
    bool            ready;    //! check ready
} glb_wdog_check_t;


/*! This structure is passed to every backend thread as a void* argument.
 *  Access to this structure is protected by lock member. */
typedef struct glb_backend_thread_ctx
{
    glb_backend_ctx_t* backend; //! global backend context
    pthread_t          id;      //! thread id
    pthread_mutex_t    lock;    //! mutex to protect access to structure
    pthread_cond_t     cond;    //! signal to thread
    char*              host;    //! address of the destination to watch
    uint16_t           port;
    glb_time_t         interval;//! check interval (nanoseconds)
    glb_wdog_check_t   result;  //! check result
    unsigned int       waiting; //! someone is waiting for result
    bool               quit;    //! signal for thread to quit
    bool               join;    //! thread is ready to be joined
    int                errn;    //! errno
} glb_backend_thread_ctx_t;


/*! Probe destination on demand. Implementation seems to be generic. */
extern void glb_backend_probe (glb_backend_thread_ctx_t* ctx,
                               glb_wdog_check_t*         res,
                               const struct timespec*    until);


/*! Backend watchdog thread. glb_backend_ctx structure will be passed to it in
 *  the void* argument. It is to poll destination supplied in addr/port at
 *  a specified interval and update check structure, setting ready member to 1.
 *  It also should respect quit member and exit if it is true. Before exit it
 *  should set join to true.
 *  See dummy implementation in glb_wdog_bakend.c */
typedef void* (*glb_backend_thread_t) (void* arg);


/*! This is a struct that needs to be initialized by backend constructor below.
 *  thd can't be null, ctx and destroy both must be either NULL or not. */
typedef struct glb_backend
{
    glb_backend_ctx_t*    ctx;     //! common backend context
    glb_backend_thread_t  thread;  //! backend loop - can't be NULL
    glb_backend_destroy_t destroy; //! backend cleanup
} glb_backend_t;


/*! Initialize global backend context and backend thread and destroy methods
 *  before starting backend threads. Return negative errno in case of error. */
typedef int (*glb_backend_init_t) (glb_backend_t* backend,
                                   const char*    init_str);

/*! Example dummy backend initializer.
 *  See glb_wdog_backend.c for implementation details. */
extern glb_backend_init_t glb_backend_dummy_init;

#endif // _glb_wdog_backend_h_
