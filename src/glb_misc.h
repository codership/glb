/*
 * Copyright (C) 2009-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_misc_h_
#define _glb_misc_h_

#include "glb_log.h"

#include <pthread.h>
#include <string.h> // strerror()

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
glb_fd_setfd (int const fd, int const flag, bool const on)
{
    int const old_flags = fcntl (fd, F_GETFD);

    if (old_flags >= 0)
    {
        int const new_flags =
            on ? (old_flags | flag) : (old_flags & ~flag);

        if (new_flags == old_flags ||
            fcntl (fd, F_SETFD, new_flags) >= 0) return 0;
    }

    return -errno;
}

static inline int
glb_fd_setfl (int const fd, int const flag, bool const on)
{
    int const old_flags = fcntl (fd, F_GETFL);

    if (old_flags >= 0)
    {
        int const new_flags =
            on ? (old_flags | flag) : (old_flags & ~flag);

        if (new_flags == old_flags ||
            fcntl (fd, F_SETFL, new_flags) >= 0) return 0;
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

#endif // _glb_misc_h_
