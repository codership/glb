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
#include <netinet/in.h> // for in_addr()
#include <arpa/inet.h>  // for inet_ntoa()
#include <string.h>     // for memcmp()

typedef struct glb_dst
{
    struct in_addr addr;         // destination IP
    ulong          port;         // destination port
    long           weight;       // >0: connection allocation weight (def: 1)
                                 //  0: no new conns, but keep existing (drain)
                                 // -1: discard destination entirely
} glb_dst_t;

/*!
 * Parse destination spec - addr[:port[:weight]]- from the string
 * @return number of fields parsed or negative error code
 */
extern long
glb_dst_parse (glb_dst_t* dst, const char* str);

static inline bool
glb_dst_equal (glb_dst_t* d1, glb_dst_t* d2)
{
    return (!memcmp (&d1->addr, &d2->addr, sizeof (d1->addr)) &&
            d1->port == d2->port);
}

static inline void
glb_dst_print (FILE* out, glb_dst_t* dst)
{
    fprintf (out, "%s:%lu,\tw: %lu\n",
             inet_ntoa(dst->addr), dst->port, dst->weight);
}

#endif // _glb_dst_h_
