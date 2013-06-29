/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_daemon_h_
#define _glb_daemon_h_

#include "glb_cnf.h"

extern void
glb_daemon_start(const glb_cnf_t* cnf);

extern void
glb_daemon_ok();

#endif // _glb_daemon_h_
