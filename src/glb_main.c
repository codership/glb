/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_cmd.h"

int main (int argc, char* argv[])
{
    glb_cmd_t* cmd = glb_cmd_parse (argc, argv);

    if (!cmd) {
        fprintf (stderr, "Failed to parse arguments. Exiting.\n");
        // glb_cmd_help(stdout, argv[0]);
        exit (EXIT_FAILURE);
    }

    glb_cmd_print (stdout, cmd);

    return 0;
}
