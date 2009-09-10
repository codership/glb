/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_misc_h_
#define _glb_misc_h_

#include "glb_log.h"

#define GLB_MUTEX_LOCK(mtx)                                             \
{                                                                       \
    int ret;                                                            \
    if ((ret = pthread_mutex_lock (mtx))) {                             \
        glb_log_fatal ("Failed to lock mutex: %d (%s)", ret, strerror(ret));\
        abort();                                                        \
    }                                                                   \
}

#define GLB_MUTEX_UNLOCK(mtx)                                           \
{                                                                       \
    int ret;                                                            \
    if ((ret = pthread_mutex_unlock (mtx))) {                           \
        glb_log_fatal ("Failed to unlock mutex: %d (%s)", ret, strerror(ret));\
        abort();                                                        \
    }                                                                   \
}

#endif // _glb_misc_h_
