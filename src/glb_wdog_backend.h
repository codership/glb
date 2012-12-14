/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * Declarations of watchdog backend interface
 *
 * $Id$
 */

#ifndef _glb_wdog_backend_h_
#define _glb_wdog_backend_h_

#include "glb_signal.h" // for extern volatile sig_atomic_t glb_terminate;

#include <stdbool.h>
#include <pthread.h>

/*! This is an opaque global backend context created by glb_backend_init_t */
typedef struct glb_backend glb_backend_t;

/*! Release global backend resources after joining all backend threads. */
typedef void           (*glb_backend_destroy_t) (glb_backend_t* backend);

typedef enum glb_dst_state
{
    GLB_DST_NOTFOUND = -1,//! destination not reachable (probably dead)
    GLB_DST_NOTREADY,     //! destination not ready to accept connections
    GLB_DST_AVOID,        //! destination better be avoided (overloaded/blocked)
    GLB_DST_READY         //! destination fully functional
} glb_dst_state_t;

typedef struct glb_wdog_check
{
    glb_dst_state_t state;    //! observed state of the destination
    double          latency;  //! communication latency (seconds)
    char*           others;   //! other cluster memebers if any in the usual fmt
    size_t          others_len; //! length of others buffer, not actual string
    bool            ready;    //! check ready
} glb_wdog_check_t;

/*! This structure is passed to every backend thread as a void* argument.
 *  Access to this structure is protected by lock member. */
typedef struct glb_backend_ctx
{
    glb_backend_t*   backend; //! global backed context
    pthread_t        id;      //! thread id
    pthread_mutex_t  lock;    //! mutex to protect access to structure
    pthread_cond_t   cond;    //! signal to thread
    char*            addr;    //! address of the destination to watch
    uint16_t         port;
    long long        interval;//! check interval (nanoseconds)
    glb_wdog_check_t result;  //! check result
    bool             quit;    //! signal for thread to quit
    bool             join;    //! thread is ready to be joined
    int              errn;    //! errno
} glb_backend_ctx_t;

/*! Backend watchdog thread. It will be passed glb_backend_ctx structure in a
 *  void* argument. It is to poll destination supplied in dest_str at
 *  a specified interval and update check structure, setting ready member to 1
 *  and signalling cond if wait member is true.
 *  It also should respect glb_terminate global variable and exit if it is
 *  non-zero. Before exit it should set join to true. */
typedef void* (*glb_backend_thread_t) (void* arg);

/*! Initialize global backend context and backend thread and destroy methods
 *  before starting backend threads. */
typedef glb_backend_t* (*glb_backend_create_t) (const char*            init_str,
                                                glb_backend_thread_t*  thd,
                                                glb_backend_destroy_t* destroy);

#endif // _glb_wdog_backend_h_
