/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_dst_h_
#define _glb_dst_h_

#include <stdlib.h>     // for ulong
#include <stdbool.h>    // for bool
#include <stdio.h>      // for FILE and fprintf()

#include "glb_socket.h"

typedef struct glb_dst
{
    glb_sockaddr_t addr;         // destination address in prepared form
    double         weight;       // >0: connection allocation weight (def: 1)
                                 //  0: no new conns, but keep existing (drain)
                                 // <0: discard destination entirely
} glb_dst_t;

/*!
 * Parse destination spec - addr[:port[:weight]]- from the string
 * @return number of fields parsed or negative error code
 */
extern long
glb_dst_parse (glb_dst_t* dst, const char* str, uint16_t default_port);

static inline void
glb_dst_set_port (glb_dst_t* dst, uint16_t port)
{
    glb_socket_addr_set_port (&dst->addr, port);
}

static inline void
glb_dst_set_weight (glb_dst_t* dst, double weight)
{
    dst->weight = weight;
}

static inline bool
glb_dst_is_equal (const glb_dst_t* d1, const glb_dst_t* d2)
{
    return (glb_socket_addr_is_equal (&d1->addr, &d2->addr));
}

static inline void
glb_dst_print (char* buf, size_t buf_len, const glb_dst_t* dst)
{
    snprintf (buf, buf_len, "%s, w: %5.3f",
              glb_socket_addr_to_string(&dst->addr), dst->weight);
    buf[buf_len - 1] = '\0';
}

#endif // _glb_dst_h_
