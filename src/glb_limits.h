/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * System limits
 *
 * $Id$
 */

#ifndef _glb_limits_h_
#define _glb_limits_h_

#include <sys/time.h>
#include <time.h>

extern int
#ifndef GLB_LIMITS
const
#endif /* GLB_LIMITS */
glb_page_size; /* System memory page size */

#define GLB_MAX_CTRL_CONN 32 // maximum connnections to control socket.

extern int glb_get_conn_limit();
extern int glb_set_conn_limit(int val);
extern void glb_limits_init();

#endif /* _glb_limits_h_ */
