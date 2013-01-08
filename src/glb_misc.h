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

#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* Even though there is currently only FD_CLOEXEC defined, it is good to
 * play safe. */
static inline int
glb_set_fd_flag (int const fd, int const flag, bool const on)
{
    int flags = fcntl (fd, F_GETFD);

    if (flags >= 0)
    {
        if (on)
            flags |= flag;
        else
            flags &= ~(flag);

        if (fcntl (fd, F_SETFD, flags) >= 0) return 0;
    }

    return -errno;
}

#if __GNUC__ >= 3
#  define GLB_LIKELY(x)   __builtin_expect((x), 1)
#  define GLB_UNLIKELY(x) __builtin_expect((x), 0)
#else
#  define GLB_LIKELY(x)   (x)
#  define GLB_UNLIKELY(x) (x)
#endif

static inline int
glb_fd_set_flag (int fd, int flag, bool on)
{
    int flags = fcntl (fd, F_GETFL);

    if (flags >= 0)
    {
        if (on && !(flags & flag))
            return fcntl (fd, F_SETFL, flags | flag);
        else if (!on && (flags & flag))
            return fcntl (fd, F_SETFL, flags & (~flag));

        return 0;
    }

    return -errno;
}

#endif // _glb_misc_h_
