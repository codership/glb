/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * System limits
 *
 * $Id$
 */

#define GLB_LIMITS

#include "glb_limits.h"
#include "glb_log.h"

#include <unistd.h>       // sysconf()
#include <errno.h>
#include <string.h>       // strerror()
#include <sys/resource.h> // getrlimit()

int glb_page_size = 1 << 12; /* 4K should be default */

int
glb_get_conn_limit()
{
    struct rlimit rlp;
    if (!getrlimit(RLIMIT_NOFILE, &rlp))
    {
        /* Each connection requires two file descriptors plus we need
         * - socket to accept client connections
         * - fifo
         * - control socket
         * - stdin/stdout (if not in daemon mode)
         * plus potential control socket connections */
        return (rlp.rlim_cur - 5 - GLB_MAX_CTRL_CONN) / 2;
    }
    else
    {
        int err = errno;
        glb_log_warn("Failed to determine open file limit: %d (%s)",
                     err, strerror(err));
        return -err;
    }
}

int
glb_set_conn_limit(int val)
{
    /* required open files limit for val connections */
    int const nofiles = (val * 2) + 5 + GLB_MAX_CTRL_CONN;

    struct rlimit rlp = { .rlim_cur = 0, .rlim_max = 0 };
    getrlimit(RLIMIT_NOFILE, &rlp);

    if (rlp.rlim_cur >= nofiles) return val; // current limits are sufficient

    int const orig_rlim_max = rlp.rlim_max;

    rlp.rlim_cur = nofiles;
    if (rlp.rlim_max < nofiles) rlp.rlim_max = nofiles;
    if (setrlimit(RLIMIT_NOFILE, &rlp))
    {
        int err = errno;
        // Most likely this is due to lack of privileges.
        // Try to raise to max possible.
        rlp.rlim_max = orig_rlim_max;
        rlp.rlim_cur = rlp.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rlp);
        val = glb_get_conn_limit();
        glb_log_warn("Failed to increase open files limit to %d: %d (%s). "
                     "Current connection limit: %d",
                     nofiles, err, strerror(err), val);
    }

    return val;
}

void glb_limits_init()
{
    long const page_size = sysconf(_SC_PAGESIZE);
    if (page_size > 0)
    {
        glb_page_size = page_size;
    }
    else
    {
        glb_log_warn("Failed to determine memory page size: %d (%s)",
                     errno, strerror(errno));
    }
}
