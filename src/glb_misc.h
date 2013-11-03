/*
 * Copyright (C) 2009-2013 Codership Oy <info@codership.com>
 *
 * $Id: glb_misc.h 156 2013-08-23 08:24:56Z vlad $
 */

#ifndef _glb_misc_h_
#define _glb_misc_h_

#include "glb_log.h"
#include "glb_macros.h"

#include <pthread.h>
#include <string.h> // strerror()
#include <stdlib.h> // abort()

static inline void GLB_MUTEX_LOCK (pthread_mutex_t* mtx)
{
    int ret;
    if (GLB_UNLIKELY((ret = pthread_mutex_lock (mtx)) != 0)) {
        glb_log_fatal ("Failed to lock mutex: %d (%s)", ret, strerror(ret));
        abort();
    }
}

static inline void GLB_MUTEX_UNLOCK (pthread_mutex_t* mtx)
{
    int ret;
    if (GLB_UNLIKELY((ret = pthread_mutex_unlock (mtx)) != 0)) {
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

/*!
 * convert string into array of tokens
 *
 * @param tok_str - input string to be parsed into tokens
 * @param sep - additional separator to whitespace
 */
extern bool
glb_parse_token_string (char*         tok_str,
                        const char*** tok_list,
                        int*          tok_num,
                        int           sep);

#if defined(__APPLE__) || defined(__FreeBSD__)
# include <errno.h>
# define ENONET ((ELAST)+1)
#endif

#endif // _glb_misc_h_
