/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stddef.h> // ptrdiff_t
#include <errno.h>
#include <assert.h>
#include <netdb.h>

#include "glb_socket.h"
#include "glb_dst.h"

// Some constants
static const int       dst_separator  = ':';
static const ptrdiff_t dst_ip_len_max = 256;
static const ptrdiff_t dst_ip_len_min = 1;
static const ulong     dst_port_max   = (1 << 16) - 1;
static const long      dst_default_weight = 1;

// parses addr:port:weight string, stores in dst
// returns number of parsed fields or negative error code
long
glb_dst_parse (glb_dst_t* dst, const char* s)
{
    const char* token;
    char*       endptr;
    char        addr_str[dst_ip_len_max + 1] = { 0, };
    ptrdiff_t   addr_len;

    dst->weight = dst_default_weight;

    // parse IP address
    endptr = strchr (s, dst_separator);

    if (NULL != endptr)
        addr_len = endptr - s;
    else
        addr_len = strlen (s);
    if (addr_len > dst_ip_len_max) {
        fprintf (stderr, "Host address too long: %s\n", s);
        return -EINVAL;
    }

    strncpy (addr_str, s, addr_len); // this now contains only host address

    if (glb_socket_in_addr (&dst->addr, addr_str)) return -EINVAL;

    if (NULL == endptr) // string is over
        return 1;

    // parse port
    assert (*endptr == dst_separator);
    token = endptr + 1;
    dst->port = strtoul (token, &endptr, 10);
    if (*endptr != dst_separator  &&
        *endptr != '\0') {
        // port field doesn't consist only of numbers
        return -EINVAL;
    }
    if (dst->port > dst_port_max) // value of 0 means no setting, don't check
        return -EINVAL;
    else if (*endptr == '\0') // string is over
        return 2;

    // parse weight
    assert (*endptr == dst_separator);
    token = endptr + 1;
    dst->weight = strtoul (token, &endptr, 10);
    if (*endptr != '\0') {
        // weight field doesn't consist only of numbers
        return -EINVAL;
    }
    return 3;
}

