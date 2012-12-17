/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_misc_h_
#define _glb_misc_h_

#include "glb_log.h"

#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static inline void GLB_MUTEX_LOCK (pthread_mutex_t* mtx)
{
    int ret;
    if ((ret = pthread_mutex_lock (mtx))) {
        glb_log_fatal ("Failed to lock mutex: %d (%s)", ret, strerror(ret));
        abort();
    }
}

static inline void GLB_MUTEX_UNLOCK (pthread_mutex_t* mtx)
{
    int ret;
    if ((ret = pthread_mutex_unlock (mtx))) {
        glb_log_fatal ("Failed to unlock mutex: %d (%s)", ret, strerror(ret));
        abort();
    }
}

#endif // _glb_misc_h_
