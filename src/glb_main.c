/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_cmd.h"
#include "glb_router.h"

int main (int argc, char* argv[])
{
    glb_cmd_t*    cmd = glb_cmd_parse (argc, argv);
    glb_router_t* router;

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

    return 0;
}
