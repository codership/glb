/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_conf.h"

int main (int argc, char* argv[])
{
    glb_conf_t* conf = glb_conf_cmd_parse (argc, argv);

    if (!conf) {
        fprintf (stderr, "Failed to parse arguments. Exiting.\n");
        exit (EXIT_FAILURE);
    }

    return 0;
}
