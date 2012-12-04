/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#define GLB_CNF_ACCESS

#include "../config.h" // for version

#include "glb_cnf.h"
#include "glb_limits.h"

glb_cnf_t* glb_cnf = NULL;

//#include <stdlib.h>
#include <string.h>

static const char default_fifo_name[]  = "/tmp/glbd.fifo";
static const int  default_threads      = 1;

const glb_cnf_t*
glb_cnf_init ()
{
    glb_cnf_t* ret = (glb_cnf_t*)malloc(sizeof(glb_cnf_t));

    if (ret)
    {
        memset (ret, 0, sizeof(*ret));
        ret->fifo_name = default_fifo_name;
        ret->n_threads = default_threads;
        ret->max_conn  = glb_get_conn_limit();
        ret->nodelay   = true;
    }
    else
    {
        fprintf (stderr, "Could not allocate %zu bytes for config struct.\n",
                 sizeof(*ret));
    }

    glb_cnf = ret;

    return ret;
}

static const char* policy_str[GLB_POLICY_MAX] =
{
    "least connected", "random", "source"
};

void
glb_cnf_print (FILE* out, const glb_cnf_t* cnf)
{
    ulong i;

    fprintf (out, "Incoming address: %s, ",
             glb_socket_addr_to_string (&cnf->inc_addr));
    fprintf (out, "control FIFO: %s\n", cnf->fifo_name);
    fprintf (out, "Control  address:  %s\n",
             cnf->ctrl_set ? glb_socket_addr_to_string (&cnf->ctrl_addr) :
             "none");
    fprintf (out, "Number of threads: %ld, max conn: %ld, policy: '%s', "
             "nodelay: %s, verbose: %s, daemon: %s\n",
             cnf->n_threads,
             cnf->max_conn,
             policy_str[cnf->policy],
             cnf->nodelay ? "ON" : "OFF",
             cnf->verbose ? "ON" : "OFF",
             cnf->daemonize ? "YES" : "NO");
    fprintf (out, "Destinations: %lu\n", (ulong)cnf->n_dst);

    for (i = 0; i < cnf->n_dst; i++) {
        char tmp[256];
        glb_dst_print (tmp, sizeof(tmp), &cnf->dst[i]);
        fprintf (out, "  %2lu: %s\n", i, tmp);
    }
}


