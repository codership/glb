/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * Read configuration parameters from command line.
 *
 * $Id$
 */

#ifndef _glb_cmd_h_
#define _glb_cmd_h_

#include "glb_cnf.h"

/*!
 * Parses command line arguments and creates global configuration structure.
 */
extern glb_cnf_t*
glb_cmd_parse (int argc, char* argv[]);

extern void
glb_cmd_help (FILE* out, const char* progname);

#endif // _glb_cmd_h_
