/*
 * Copyright (C) 2009-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_misc_h_
#define _glb_misc_h_

#include "glb_log.h"

#include <stdbool.h>
#include <fcntl.h>

#if __GNUC__ >= 3
#  define GLB_LIKELY(x)   __builtin_expect((x), 1)
#  define GLB_UNLIKELY(x) __builtin_expect((x), 0)
#else
#  define GLB_LIKELY(x)   (x)
#  define GLB_UNLIKELY(x) (x)
#endif

#define GLB_MUTEX_LOCK(mtx)                                             \
{                                                                       \
    int ret;                                                            \
    if (GLB_UNLIKELY((ret = pthread_mutex_lock (mtx)) != 0)) {          \
        glb_log_fatal ("Failed to lock mutex: %d (%s)", ret, strerror(ret));\
        abort();                                                        \
    }                                                                   \
}

#define GLB_MUTEX_UNLOCK(mtx)                                           \
{                                                                       \
    int ret;                                                            \
    if (GLB_UNLIKELY((ret = pthread_mutex_unlock (mtx)) != 0)) {        \
        glb_log_fatal ("Failed to unlock mutex: %d (%s)", ret, strerror(ret));\
        abort();                                                        \
    }                                                                   \
}

static inline int
glb_fd_set_flag (int fd, int flag, bool on)
{
    int flags = fcntl (fd, F_GETFL);

    if (on && !(flags & flag))
        return fcntl (fd, F_SETFL, flags | flag);
    else if (!on && (flags & flag))
        return fcntl (fd, F_SETFL, flags & (~flag));

    return 0;
}
#endif // _glb_misc_h_
