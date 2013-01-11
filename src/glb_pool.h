/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_pool_h_
#define _glb_pool_h_

#include "glb_socket.h"
#include "glb_router.h"
#include "glb_pool_stats.h"

typedef struct glb_pool glb_pool_t;

// Creates array of routing pools, each pool is serviced by a separate thread
extern glb_pool_t*
glb_pool_create (const glb_cnf_t* cnf, glb_router_t* router);

extern void
glb_pool_destroy (glb_pool_t* pool);

// Adds connection to conneciton pool
extern int
glb_pool_add_conn (glb_pool_t*           pool,
                   int                   inc_sock,
                   const glb_sockaddr_t* inc_addr,
                   int                   dst_sock,
                   const glb_sockaddr_t* dst_addr,
                   bool                  complete);

// Closes all connecitons to a given destination
extern int
glb_pool_drop_dst (glb_pool_t* pool, const glb_sockaddr_t* dst);

extern ssize_t
glb_pool_print_stats (glb_pool_t* pool, char* buf, size_t buf_len);

extern ssize_t
glb_pool_print_info (glb_pool_t* pool, char* buf, size_t buf_len);

#endif // _glb_pool_h_
