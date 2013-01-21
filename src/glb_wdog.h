/*
 * Copyright (C) 2012-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_wdog_h_
#define _glb_wdog_h_

#include "glb_cnf.h"
#include "glb_router.h"
#include "glb_pool.h"

typedef struct glb_wdog glb_wdog_t;

extern glb_wdog_t*
glb_wdog_create (const glb_cnf_t* cnf, glb_router_t* router, glb_pool_t* pool);

extern int
glb_wdog_change_dst (glb_wdog_t* wdog, const glb_dst_t* dst);

extern void
glb_wdog_destroy (glb_wdog_t* wdog);

extern size_t
glb_wdog_print_info (glb_wdog_t* wdog, char* buf, size_t buf_len);

#endif // _glb_wdog_h_
