/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stddef.h> // ptrdiff_t
#include <errno.h>
#include <assert.h>

#include "glb_socket.h"
#include "glb_dst.h"

// Some constants
#define dst_separator      ':'
#define dst_ip_len_max     256
#define dst_ip_len_min     1
#define dst_port_max       ((1 << 16) - 1)
#define dst_default_weight 1.0

// parses addr:port:weight string, stores in dst
// returns number of parsed fields or negative error code
long
glb_dst_parse (glb_dst_t* dst, const char* s, uint16_t default_port)
{
    const char* token;
    char*       endptr;
    char        addr_str[dst_ip_len_max + 1] = { 0, };
    ptrdiff_t   addr_len;
    ulong       port = default_port;
    long        ret  = 0;

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

    ret = 1;
    if (NULL == endptr) // string is over
        goto end;

    // parse port
    assert (*endptr == dst_separator);
    token = endptr + 1;
    port = strtoul (token, &endptr, 10);
    if (*endptr != dst_separator  &&
        *endptr != '\0') {
        perror ("Port field doesn't consist only of numbers");
        return -EINVAL;
    }
    if (port > dst_port_max) {
        // value of 0 means no setting, don't check
        perror ("Port value exceeds maximum port number");
        return -EINVAL;
    }

    ret = 2;
    if (*endptr == '\0') // string is over
        goto end;

    // parse weight
    assert (*endptr == dst_separator);
    token = endptr + 1;
    dst->weight = strtod (token, &endptr);
    if (*endptr != '\0') {
        perror ("Weight field doesn't consist only of numbers");
        return -EINVAL;
    }
    ret = 3;

end:
    if (glb_socket_addr_init (&dst->addr, addr_str, port)) {
        perror ("glb_socket_addr_init()");
        return -EINVAL;
    }

    return ret;
}

