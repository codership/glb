/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <sys/select.h> // for select() and FD_SET

typedef struct pool
{
    pthread_mutex_t lock;
    size_t          n_conns;
    fd_set          fd;
    fd_set          fd_read;
    int             route_map[FD_SETSIZE];
} pool_t;

struct glb_pool
{
    pthread_mutex_t lock;
    size_t          n_pools;
    pool_t          pool[];  // pool array, can't be changed in runtime
};

#include "glb_pool.h"

static long
pool_init (pool_t* pool)
{
}

extern glb_pool_t*
glb_pool_create (size_t pools);

extern void
glb_pool_destroy (glb_pool_t* pool);

extern long
glb_pool_add_conn (glb_pool_t* pool);

extern long
glb_pool_drop_dst (glb_pool_t* pool, const glb_sockaddr_t* dst);

