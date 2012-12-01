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
glb_max_conn;  /* Maximum connections that can be accepted: (nofile - 2)/2 */

extern int
#ifndef GLB_LIMITS
const
#endif /* GLB_LIMITS */
glb_page_size; /* System memory page size */

extern void glb_limits_init();

#endif /* _glb_limits_h_ */
