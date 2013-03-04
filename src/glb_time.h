/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_time_h_
#define _glb_time_h_

#include <time.h>
#include <sys/time.h>
#include <assert.h>

typedef long long glb_time_t;

/*! Returns current time as "elapsed" nanoseconds. */
static inline glb_time_t
glb_time_now()
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (tv.tv_sec * 1000000000LL + tv.tv_usec * 1000);
}

/*! Returns current time in struct timespec format */
static inline struct timespec
glb_timespec_now()
{
    struct timeval tv;
    gettimeofday (&tv, NULL);

    /* it seems that we can't simply cast struct timeval to struct timespec
     * as they potentially may have different types for members. */
    struct timespec ts = {
        .tv_sec  = tv.tv_sec,
        .tv_nsec = tv.tv_usec * 1000
    };

    return ts;
}

/*! A helper to add glb_time_t interval to struct timespec date */
static inline void
glb_timespec_add (struct timespec* t, glb_time_t i)
{
    i += t->tv_nsec;
    t->tv_sec += i / 1000000000;
    t->tv_nsec = i % 1000000000;
}

static inline struct timespec
glb_time_to_timespec (glb_time_t t)
{
    struct timespec ts;
    ts.tv_sec  = t / 1000000000;
    ts.tv_nsec = t % 1000000000;
    return ts;
}

/*! Convert glb_time_t to seconds */
static inline double
glb_time_seconds (glb_time_t t)
{
    return (t * 1.0e-09);
}

/*! Convert glb_time_t to seconds (approx) */
static inline long
glb_time_approx_seconds (glb_time_t t)
{
    assert (t >= 0);
    return (t >> 30);
}

/*! Convert double seconds to glb_time_h */
static inline glb_time_t
glb_time_from_double (double sec)
{
    return (sec * 1000000000LL);
}

#endif // _glb_time_h_
