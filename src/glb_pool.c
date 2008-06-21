/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdlib.h>
#include <sys/select.h> // for select() and FD_SET
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "glb_pool.h"

#define pool_buf_size 32768

typedef struct pool_conn
{
    int             fd;  // fd where to send
    size_t          sent;
    size_t          rcvd;
    uint8_t         buf[pool_buf_size];
    glb_sockaddr_t  dst; // destinaiton id
} pool_conn_t;

typedef struct pool
{
    pthread_t       thread;
    int             ctl_recv; // receive commands - pool thread
    int             ctl_send; // send commands to pool - other function
    size_t          n_conns;
    fd_set          fds_ref;  // reference fd_set, to initialize fds_read
    fd_set          fds_read;
    fd_set          fds_write;
    int             fd_max;
    pool_conn_t*    route_map[FD_SETSIZE]; // looking for connection ctx by fd
} pool_t;

struct glb_pool
{
    pthread_mutex_t lock;
    size_t          n_pools;
    pool_t          pool[];  // pool array, can't be changed in runtime
};

static void*
pool_thread (void* arg)
{
    pool_t* pool = arg;
    struct timeval timeout = { 1, 0 }; // 1 second
    struct timeval tmp;

    while (1) {
        tmp = timeout;
        memcpy (&pool->fds_read, &pool->fds_ref, sizeof (fd_set));
        select (pool->fd_max+1, &pool->fds_read, &pool->fds_write, NULL, &tmp);

        printf ("Thread working\n");
    }

    return NULL;
}

static long
pool_init (pool_t* pool)
{
    long ret;
    int pipe_fds[2];

    ret = pipe(pipe_fds);
    if (ret) {
        perror ("Failed to open control pipe");
        return -ret;
    }

    pool->ctl_recv = pipe_fds[0];
    pool->ctl_send = pipe_fds[1];

    FD_ZERO (&pool->fds_ref);
    FD_ZERO (&pool->fds_write);
    FD_SET  (pool->ctl_recv, &pool->fds_ref);
    pool->fd_max = pool->ctl_recv;

    ret = pthread_create (&pool->thread, NULL, pool_thread, pool);
    if (ret) {
        perror ("Failed to create thread");
        return -ret;
    }

    return 0;
}

extern glb_pool_t*
glb_pool_create (size_t n_pools)
{
    size_t ret_size = sizeof(glb_pool_t) + n_pools * sizeof(pool_t);
    glb_pool_t* ret = malloc (ret_size);

    if (ret) {
        long err;
        size_t i;

        memset (ret, 0, ret_size);
        pthread_mutex_init (&ret->lock, NULL);
        ret->n_pools = n_pools;

        for (i = 0; i < n_pools; i++) {
            if ((err = pool_init(&ret->pool[i]))) {
                fprintf (stderr, "Failed to initialize pool %zu\n", i);
                abort();
            }
        }
    }
    else {
        fprintf (stderr, "Could not allocate memory for %zu pools\n", n_pools);
        abort();
    }

    return ret;
}

extern void
glb_pool_destroy (glb_pool_t* pool)
{
    fprintf (stderr, "glb_pool_destroy() not implemented yet!");
}

extern long
glb_pool_add_conn (glb_pool_t* pool)
{
    return 0;
}

extern long
glb_pool_drop_dst (glb_pool_t* pool, const glb_sockaddr_t* dst)
{
    return 0;
}

