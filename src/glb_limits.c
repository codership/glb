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


int glb_max_conn  = 0;
int glb_page_size = 1 << 12; /* 4K should be default */

void glb_limits_init()
{
    struct rlimit rlp;
    if (!getrlimit(RLIMIT_NOFILE, &rlp))
    {
        /* Each connection requires two file descriptors plus we need
         * - socket to accept client connections
         * - fifo
         * - control socket
         * - stdin/stdout (if not in daemon mode)
         * plus control socket connection, so let it be 6. */
        glb_max_conn = (rlp.rlim_cur - 6) / 2;
    }
    else
    {
        glb_log_warn("Failed to determine open file limit: %d (%s)",
                     errno, strerror(errno));
    }

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
