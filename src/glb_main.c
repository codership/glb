/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <unistd.h> // for sleep()

#include "glb_cmd.h"
#include "glb_router.h"
#include "glb_pool.h"

glb_router_t* router = NULL;
glb_pool_t*   pool   = NULL;

int main (int argc, char* argv[])
{
    glb_cmd_t*    cmd = glb_cmd_parse (argc, argv);

    if (!cmd) {
        fprintf (stderr, "Failed to parse arguments. Exiting.\n");
        exit (EXIT_FAILURE);
    }

    glb_cmd_print (stdout, cmd);

    router = glb_router_create (cmd->n_dst, cmd->dst);
    if (!router) {
        fprintf (stderr, "Failed to create router. Exiting.\n");
        exit (EXIT_FAILURE);
    }

    pool = glb_pool_create (cmd->n_threads, router);
    if (!pool) {
        fprintf (stderr, "Failed to create thread pool. Exiting.\n");
        exit (EXIT_FAILURE);
    }

    sleep (3);

    return 0;
}
