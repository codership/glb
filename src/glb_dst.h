/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id: glb_dst.h 160 2013-11-03 14:49:02Z alex $
 */

#ifndef _glb_dst_h_
#define _glb_dst_h_

#include <stdbool.h>    // for bool
#include <stdio.h>      // for snprintf()

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
    glb_sockaddr_set_port (&dst->addr, port);
}

static inline void
glb_dst_set_weight (glb_dst_t* dst, double weight)
{
    dst->weight = weight;
}

static inline bool
glb_dst_is_equal (const glb_dst_t* d1, const glb_dst_t* d2)
{
    return (glb_sockaddr_is_equal (&d1->addr, &d2->addr));
}

static inline void
glb_dst_print (char* buf, size_t buf_len, const glb_dst_t* dst)
{
    glb_sockaddr_str_t addr = glb_sockaddr_to_astr (&dst->addr);
    snprintf (buf, buf_len, "%s, w: %5.3f",
              addr.str, dst->weight);
    buf[buf_len - 1] = '\0';
}

#endif // _glb_dst_h_
