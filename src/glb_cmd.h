/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_cmd_h_
#define _glb_cmd_h_

#include "glb_cnf.h"

/*!
 * Parses command line arguments and creates configuration structure
 * passed in defaults.
 *
 * @return updated config structure (maybe realloced) or NULL in case of failure
 */
extern void
glb_cmd_parse (int argc, char* argv[]);

extern void
glb_cmd_help (FILE* out, const char* progname);

#endif // _glb_cmd_h_
