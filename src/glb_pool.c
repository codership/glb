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
#include <errno.h>

#include "glb_pool.h"

typedef enum pool_ctl_code
{
    POOL_CTL_ADD_CONN,
    POOL_CTL_DEL_DST,
    POOL_CTL_SHUTDOWN,
    POOL_CTL_MAX
} pool_ctl_code_t;

typedef struct pool_ctl
{
    pool_ctl_code_t code;
    void*           data;
} pool_ctl_t;

typedef struct pool_conn_end
{
    bool            inc;      // to differentiate between the ends
    int             sock;     // fd of connection
    size_t          sent;
    size_t          total;
    glb_sockaddr_t  dst_addr; // destinaiton id
    uint8_t         buf[];    // has pool_buf_size
} pool_conn_end_t;

// we want to allocate memory for both ends in one malloc() call and have it
// nicely aligned.
#define pool_conn_size 4096 // presumably a page and should be enough for
                            // two ethernet frames (what about jumbo?)
const size_t pool_buf_size  = (pool_conn_size/2 - sizeof(pool_conn_end_t));

typedef struct pool
{
    pthread_t       thread;
    int             ctl_recv; // receive commands - pool thread
    int             ctl_send; // send commands to pool - other function
    volatile ulong  n_conns;  // how many connecitons this pool serves
    fd_set          fds_ref;  // reference fd_set, to initialize fds_read
    fd_set          fds_read;
    fd_set          fds_write;
    int             fd_max;
    pool_conn_end_t* route_map[FD_SETSIZE]; // looking for connection ctx by fd
} pool_t;

struct glb_pool
{
    pthread_mutex_t lock;
    ulong           n_pools;
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

// finds the least busy pool
static inline pool_t*
pool_get_pool (glb_pool_t* pool)
{
    pool_t* ret     = pool->pool;
    ulong min_conns = ret->n_conns;
    register ulong i;

    for (i = 1; i < pool->n_pools; i++) {
        if (min_conns > pool->pool[i].n_conns) {
            min_conns = pool->pool[i].n_conns;
            ret = pool->pool + i;
        }
    }
    return ret;
}

extern long
glb_pool_add_conn (glb_pool_t*     pool,
                   int             inc_sock,
                   int             dst_sock,
                   glb_sockaddr_t* dst_addr)
{
    pool_t* p     = pool_get_pool (pool);
    long    ret   = -ENOMEM;
    void*   route = malloc (pool_conn_size);

    if (route) {
        pool_conn_end_t* inc_end = route;
        pool_conn_end_t* dst_end = route + pool_conn_size / 2;
        pool_ctl_t       add_conn_ctl = { POOL_CTL_ADD_CONN, route };

        inc_end->inc      = true;
        inc_end->sock     = inc_sock;
        inc_end->sent     = 0;
        inc_end->total    = 0;
        inc_end->dst_addr = *dst_addr; // needed for cleanups

        dst_end->inc      = false;
        dst_end->sock     = dst_sock;
        dst_end->sent     = 0;
        dst_end->total    = 0;
        dst_end->dst_addr = *dst_addr; // needed for cleanups

        ret = write (p->ctl_send, &add_conn_ctl, sizeof (add_conn_ctl));
        if (ret != sizeof (add_conn_ctl)) {
            perror ("Sending add_conn_ctl failed");
            if (ret > 0) abort(); // partial ctl was sent, don't know what to do
        }
        else ret = 0;
    }

    return ret;
}

extern long
glb_pool_drop_dst (glb_pool_t* pool, const glb_sockaddr_t* dst)
{
    return 0;
}

