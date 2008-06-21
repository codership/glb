/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_pool_h_
#define _glb_pool_h_

#include "glb_socket.h"

typedef struct glb_pool glb_pool_t;

// Creates array of routing pools, each pool is serviced by a separate thread
extern glb_pool_t*
glb_pool_create (size_t pools);

extern void
glb_pool_destroy (glb_pool_t* pool);

// Adds connection to conneciton pool
extern long
glb_pool_add_conn (glb_pool_t*     pool,
                   int             inc_sock,
                   int             dst_sock,
                   glb_sockaddr_t* dst_addr);

// Closes all connecitons to a given address
extern long
glb_pool_drop_dst (glb_pool_t* pool, const glb_sockaddr_t* dst);

#endif // _glb_pool_h_
