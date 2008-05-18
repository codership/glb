/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_conf_h_
#define _glb_conf_h_

#include <stdlib.h>
#include <stdbool.h>

// Destination specification
typedef struct glb_conf_dst
{
    ulong ip;                     // destination IP
    ulong port;                   // destination port
    long  weight;                 // >0: connection allocation weight (def: 1)
                                  //  0: no new conns, but keep existing (drain)
                                  // -1: discard destination entirely
} glb_conf_dst_t;

typedef struct glb_conf
{
    ulong          inc_ip;       // IP to bind listener to
    ulong          inc_port;     // port to listen at
    ulong          n_threads;    // number of routing threads (1 .. oo)
    bool           src_tracking; // connect to the same dst for the same src?
    size_t         n_dst;        // number of destinations
    glb_conf_dst_t dst[];        // destination descriptions
} glb_conf_t;

extern glb_conf_t*
glb_conf_cmd_parse (int argc, char* argv[]);

#endif // _glb_config_h_
