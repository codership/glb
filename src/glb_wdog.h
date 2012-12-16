/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_wdog_h_
#define _glb_wdog_h_

#include "glb_cnf.h"
#include "glb_router.h"

typedef struct glb_wdog glb_wdog_t;

extern glb_wdog_t*
glb_wdog_create  (const glb_cnf_t* cnf, glb_router_t* router);

extern int
glb_wdog_change_dst (glb_wdog_t* wdog, const glb_dst_t* dst, bool explicit);

extern void
glb_wdog_destroy (glb_wdog_t* wdog);

#endif // _glb_wdog_h_
