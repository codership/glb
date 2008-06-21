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

#include "glb_dst.h"

typedef struct glb_cmd
{
    glb_sockaddr_t inc_addr;     // IP to bind listener for incoming connecitons
    glb_sockaddr_t ctrl_addr;    // control connection interface (127.0.0.1)
    ulong          n_threads;    // number of routing threads (1 .. oo)
    bool           src_tracking; // connect to the same dst for the same src?
    bool           verbose;      // connect to the same dst for the same src?
    size_t         n_dst;        // number of destinations
    glb_dst_t      dst[];         // destination descriptions
} glb_cmd_t;

extern glb_cmd_t*
glb_cmd_parse (int argc, char* argv[]);

extern void
glb_cmd_print (FILE* out, glb_cmd_t* conf);

extern void
glb_cmd_help (FILE* out, const char* progname);

#endif // _glb_cmd_h_
