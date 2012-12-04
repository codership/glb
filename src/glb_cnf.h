/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_cnf_h_
#define _glb_cnf_h_

#include "glb_dst.h"

#include <stdbool.h>    // for bool
#include <stdio.h>      // for FILE and fprintf()

typedef enum glb_policy
{
    GLB_POLICY_LEAST = 0, /* least conected */
    GLB_POLICY_RANDOM,    /* random choice */
    GLB_POLICY_SOURCE,    /* same for one source */
    GLB_POLICY_MAX
} glb_policy_t;

typedef struct glb_cnf
{
    glb_sockaddr_t inc_addr;     // IP to bind listener for incoming connecitons
    glb_sockaddr_t ctrl_addr;    // network control interface
    bool           ctrl_set;     // was set? (false)
    const char*    fifo_name;    // FIFO file name
    long           n_threads;    // number of routing threads (1 .. oo)
    long           max_conn;     // max allowed client connections
    bool           nodelay;
    glb_policy_t   policy;       // algorithm to use for load-balancing
    bool           verbose;      // be verbose?
    bool           daemonize;    // become a daemon?
    size_t         n_dst;        // number of destinations
    glb_dst_t      dst[];        // destination descriptions
} glb_cnf_t;

#ifndef GLB_CNF_ACCESS
extern const glb_cnf_t* const glb_cnf;
#else
extern glb_cnf_t* glb_cnf;
#endif /* GLB_CNF_ACCESS */

extern const glb_cnf_t*
glb_cnf_init ();

extern void
glb_cnf_print (FILE* out, const glb_cnf_t* cnf);

#endif // _glb_cnf_h_
