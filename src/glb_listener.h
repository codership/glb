/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_listener_h_
#define _glb_listener_h_

#include "glb_router.h"
#include "glb_pool.h"

typedef struct glb_listener glb_listener_t;

extern glb_listener_t*
glb_listener_create (const glb_cnf_t* cnf,
                     glb_router_t*    router,
                     glb_pool_t*      pool,
                     int              listen_sock);

extern void
glb_listener_destroy (glb_listener_t* listener);

#endif // _glb_listener_h_
