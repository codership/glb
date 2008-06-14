/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_cmd_h_
#define _glb_cmd_h_

#include <stdlib.h>     // for ulong
#include <stdbool.h>    // for bool
#include <stdio.h>      // for FILE and fprintf()
#include <netinet/in.h> // for in_addr

// Destination configuration data
typedef struct glb_cmd_dst
{
    struct in_addr addr;         // destination IP
    ulong          port;         // destination port
    long           weight;       // >0: connection allocation weight (def: 1)
                                 //  0: no new conns, but keep existing (drain)
                                 // -1: discard destination entirely
} glb_cmd_dst_t;

typedef struct glb_conf
{
    struct in_addr inc_addr;     // IP to bind listener for incoming connecitons
    ulong          inc_port;     // port to listen at (no default)
    struct in_addr ctrl_addr;    // control connection interface (127.0.0.1)
    ulong          ctrl_port;    // control connection port (inc_port + 1)
    ulong          n_threads;    // number of routing threads (1 .. oo)
    bool           src_tracking; // connect to the same dst for the same src?
    bool           verbose;      // connect to the same dst for the same src?
    size_t         n_dst;        // number of destinations
    glb_cmd_dst_t dst[];        // destination descriptions
} glb_cmd_t;

extern glb_cmd_t*
glb_cmd_cmd_parse (int argc, char* argv[]);

extern void
glb_cmd_print (FILE* out, glb_cmd_t* conf);

extern void
glb_cmd_help (FILE* out, const char* progname);

#endif // _glb_cmd_h_
