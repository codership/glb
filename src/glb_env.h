/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * Read configuration parameters from environment variables
 *
 * $Id$
 */

#ifndef _glb_env_h_
#define _glb_env_h_

#include "glb_cnf.h"

/*!
 * Parses environment variables and creates configuration structure.
 */
extern glb_cnf_t*
glb_env_parse ();

#endif // _glb_env_h_
