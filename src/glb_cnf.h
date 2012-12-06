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
    glb_policy_t   policy;       // algorithm to use for load-balancing
    bool           nodelay;      // use TCP_NODELAY?
    bool           defer_accept; // use TCP_DEFER_ACCEPT?
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

extern glb_cnf_t*
glb_cnf_init ();

/*!
 * parses array list of destinations, returns updated cnf structure.
 */
extern glb_cnf_t*
glb_parse_dst_list (const char* const dst_list[],
                    int         const n_dst,
                    uint16_t    const default_port,
                    glb_cnf_t*  const in);

/*!
 * parses [addr:]port
 *
 * depending on the purpose default address can be e.g. 127.0.0.1 or 0.0.0.0
 * (for listening socket)
 */
extern int
glb_parse_addr (glb_sockaddr_t* addr,
                const char*     str,
                const char*     default_addr);

extern void
glb_print_version (FILE* out);

extern void
glb_cnf_print (FILE* out, const glb_cnf_t* cnf);

#endif // _glb_cnf_h_
