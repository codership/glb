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
#include <assert.h>
#include <sys/time.h>

#ifdef GLB_USE_SPLICE
#include <fcntl.h>
#endif

#include "glb_pool.h"

extern bool glb_verbose;

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

typedef struct pool_stats
{
    size_t recv_bytes;
    size_t n_recv;
    size_t send_bytes;
    size_t n_send;
    size_t sel_reads;
    size_t sel_writes;
    size_t n_select;
} pool_stats_t;

#ifdef GLB_POOL_STATS
static pool_stats_t zero_stats = { 0, };
#endif

typedef struct pool_conn_end
{
    bool            inc;      // to differentiate between the ends
    int             sock;     // fd of connection
    size_t          sent;
    size_t          total;
    glb_sockaddr_t  dst_addr; // destinaiton id
#ifdef GLB_USE_SPLICE
    int             splice[2];
#endif
    uint8_t         buf[];    // has pool_buf_size
} pool_conn_end_t;

// we want to allocate memory for both ends in one malloc() call and have it
// nicely aligned.
#define pool_conn_size 8192 // presumably a page multiple and should be enough
                            // for two ethernet frames (what about jumbo?)
const size_t pool_buf_size  = (pool_conn_size/2 - sizeof(pool_conn_end_t));

typedef struct pool
{
    long            id;
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    int             ctl_recv; // receive commands - pool thread
    int             ctl_send; // send commands to pool - other function
    volatile ulong  n_conns;  // how many connecitons this pool serves
    fd_set          fds_read;
    fd_set          fds_write;
    int             fd_max;
    int             fd_min;
    glb_router_t*   router;
#ifdef GLB_POOL_STATS
    volatile pool_stats_t stats;
#endif
    pool_conn_end_t* route_map[FD_SETSIZE]; // looking for connection ctx by fd
} pool_t;

struct glb_pool
{
    pthread_mutex_t lock;
    ulong           n_pools;
    struct timeval  begin;
    pool_t          pool[];  // pool array, can't be changed in runtime
};

static inline void
pool_set_conn_end (pool_t* pool, pool_conn_end_t* end1, pool_conn_end_t* end2)
{
    assert (end1->sock < FD_SETSIZE);
    assert (NULL == pool->route_map[end1->sock]);
    pool->route_map[end1->sock] = end2;
    if (end1->sock > pool->fd_max) pool->fd_max = end1->sock;
    if (end1->sock < pool->fd_min) pool->fd_min = end1->sock;
    FD_SET (end1->sock, &pool->fds_read);
}

static long
pool_handle_ctl (pool_t* pool, pool_ctl_t* ctl)
{
    switch (ctl->code) {
    case POOL_CTL_ADD_CONN:
    {
        pool_conn_end_t* inc_end = ctl->data;
        pool_conn_end_t* dst_end = ctl->data + pool_conn_size/2;

        pool_set_conn_end (pool, inc_end, dst_end);
        pool_set_conn_end (pool, dst_end, inc_end);

        pool->n_conns++; // increment connection count
        if (glb_verbose) {
            fprintf (stderr,
                     "Pool %ld: added connection, "
                     "(total pool connections: %ld)\n",
                     pool->id, pool->n_conns);
        }
    }
    break;
    default: // nothing else is implemented
        fprintf (stderr, "Unsupported CTL: %d\n", ctl->code);
    }

    // Notify ctl sender
    pthread_mutex_lock (&pool->lock);
    pthread_cond_signal (&pool->cond);
    pthread_mutex_unlock (&pool->lock);

    return 0;
}

// removing traces of connection end - reverse to what pool_set_conn_end() did
static inline void
pool_reset_conn_end (pool_t* pool, int fd)
{
    int i;

    FD_CLR (fd, &pool->fds_read);
    FD_CLR (fd, &pool->fds_write);
    pool->route_map[fd] = NULL;

    if (fd == pool->fd_max) {
        // fd_max can't be less than pool->ctl_recv, because of select()
        for (i = fd - 1; i > pool->ctl_recv; i--) {
            if (pool->route_map[i] != NULL) break;
        }
        pool->fd_max = i;
    }

    if (fd == pool->fd_min) {
        for (i = fd + 1; i < pool->fd_max; i++) {
            if (pool->route_map[i] != NULL) break;
        }
        pool->fd_min = i;
    }

    close (fd);
}

static void
pool_remove_conn (pool_t* pool, int src_fd)
{
    pool_conn_end_t* dst    = pool->route_map[src_fd];
    int              dst_fd = dst->sock;
    pool_conn_end_t* src    = pool->route_map[dst_fd];

    pool->n_conns--;

    if (glb_verbose) {
        fprintf (stderr, "Pool %ld: disconnecting from %s "
                 "(total pool connections: %ld)\n", pool->id,
                 glb_socket_addr_to_string (&dst->dst_addr), pool->n_conns);
    }
    glb_router_disconnect (pool->router, &dst->dst_addr);

#ifdef GLB_USE_SPLICE
    close (dst->splice[0]); close (dst->splice[1]);
    close (src->splice[0]); close (src->splice[1]);
#endif

    if (dst->inc) {
        free (dst); // frees both ends
    }
    else {
        assert (src->inc);
        free (src);
    }

    pool_reset_conn_end (pool, src_fd);
    pool_reset_conn_end (pool, dst_fd);
}

static inline ssize_t
pool_send_data (pool_t* pool, pool_conn_end_t* dst)
{
    ssize_t ret;

#ifndef GLB_USE_SPLICE
    ret = send (dst->sock, &dst->buf[dst->sent], dst->total - dst->sent,
                MSG_DONTWAIT);
#else
    ret = splice (dst->splice[0], NULL, dst->sock, NULL,
                  dst->total - dst->sent, SPLICE_F_NONBLOCK);
#endif

    if (ret > 0) {
#ifdef GLB_POOL_STATS
        pool->stats.send_bytes += ret;
#endif
        dst->sent += ret;
        if (dst->sent == dst->total) { // all data sent, reset pointers
            dst->sent =  dst->total = 0;
            FD_CLR (dst->sock, &pool->fds_write);
        }
        else { // hm, will it work? - remind to send later
            switch (errno) {
            case EAGAIN:
                FD_SET (dst->sock, &pool->fds_write);
                ret = 0;
                break;
            }
        }
#ifdef GLB_POOL_STATS
        pool->stats.n_send++;
#endif
    }

    return ret;
}

// inline because frequent
static inline ssize_t
pool_handle_read (pool_t* pool, int src_fd)
{
    ssize_t ret = 0;
    pool_conn_end_t* dst = pool->route_map[src_fd];

    // first, try read data from source, if there's enough space
    if (dst->total < pool_buf_size) {
#ifndef GLB_USE_SPLICE
        ret = recv (src_fd, &dst->buf[dst->total], pool_buf_size - dst->total,
                    0);
#else
        ret = splice (src_fd, NULL, dst->splice[1], NULL,
                      pool_buf_size - dst->total,
                      SPLICE_F_MORE | SPLICE_F_MOVE);
#endif
        if (ret > 0) {
            dst->total += ret;
            // now try to send whatever we have
            if (pool_send_data (pool, dst) < 0) {
                // probably don't care what error is
                perror ("pool_handle_read(): sending data");
            }
#ifdef GLB_POOL_STATS
            pool->stats.recv_bytes += ret;
#endif
        }
        else {
            if (0 == ret) { // socket closed, must close another end and cleanup
                pool_remove_conn (pool, src_fd);
                ret = -1;
            }
            else { // some other error
                if (errno != EAGAIN)
                    perror ("pool_handle_read(): receiving data");
            }
        }
#ifdef GLB_POOL_STATS
        pool->stats.n_recv++;
#endif
    }
    return ret;
}

static inline ssize_t
pool_handle_write (pool_t* pool, int dst_fd)
{
    register int     src_fd = pool->route_map[dst_fd]->sock;
    pool_conn_end_t* dst    = pool->route_map[src_fd];

    if (dst->total) {
        assert (dst->total > dst->sent);
        if (pool_send_data (pool, dst) < 0) {
            // probably don't care what error is
            perror ("pool_handle_read(): sending data error");
        }
    }
    return 0;
}

static void*
pool_thread (void* arg)
{
    pool_t* pool   = arg;

    // synchronize with the calling process
    pthread_mutex_lock (&pool->lock);
    pthread_mutex_unlock (&pool->lock);

    while (1) {
        long ret;
        fd_set fds_read, fds_write;

        fds_read  = pool->fds_read;
        fds_write = pool->fds_write;
        ret = select (pool->fd_max+1, &fds_read, &fds_write, NULL, NULL);

        if (ret > 0) { // we have some input
            long count = ret;
            int  fd    = pool->fd_min;
            // first, check ctl pipe
            if (FD_ISSET (pool->ctl_recv, &fds_read)) {
                pool_ctl_t ctl;

                ret = read (pool->ctl_recv, &ctl, sizeof(ctl));
                if (sizeof(ctl) == ret) { // complete ctl read
                    pool_handle_ctl (pool, &ctl);
                }
                else { // should never happen!
                    perror ("Pool: incomplete read from ctl");
                    abort();
                }
#ifdef GLB_POOL_STATS
                pool->stats.sel_reads++;
#endif
                /*
                 * pool->ctl_recv can theoretically get between fd_min and
                 * fd_max.
                 * For simplicity we want to assume that it is not set when
                 * processing normal fds
                 * also set of connections could be modified by ctl,
                 * so start over
                 */
                goto end;
            }

            assert (!FD_ISSET (pool->ctl_recv, &fds_read));

            // check remaining connections
            while (count) {
                assert (fd <= pool->fd_max);

                while (NULL == pool->route_map[fd]) fd++;

                /*
                 * If pool_handle_read() or pool_handle_write() below
                 * return error, this is most likely because connection was
                 * closed. In that case cleanup has happend and fd set has
                 * changed. Break out of the loop to start over again. 
                 */ 
                if (FD_ISSET (fd, &fds_read)) {
#ifdef GLB_POOL_STATS
                    pool->stats.sel_reads++;
#endif
                    if (pool_handle_read (pool, fd) < 0) goto end;
                    count--;
                }

                if (FD_ISSET (fd, &fds_write)) {
#ifdef GLB_POOL_STATS
                    pool->stats.sel_writes++;
#endif
                    if (pool_handle_write (pool, fd) < 0) goto end;
                    count--;
                }

                fd++;
            }
        }
        else if (-1 == ret) {
            perror ("select() failed");
        }
        else {
            // timed out
            //printf ("Thread %ld is idle\n", pool->id);
        }
    end:
#ifdef GLB_POOL_STATS
        pool->stats.n_select++;
#endif
        continue;
    }

    return NULL;
}

static long
pool_init (pool_t* pool, long id, glb_router_t* router)
{
    long ret;
    int pipe_fds[2];

    pool->id     = id;
    pool->router = router;

    pthread_mutex_init (&pool->lock, NULL);
    pthread_cond_init  (&pool->cond, NULL);

    ret = pipe(pipe_fds);
    if (ret) {
        perror ("Failed to open control pipe");
        return -ret;
    }

    pool->ctl_recv = pipe_fds[0];
    pool->ctl_send = pipe_fds[1];

    FD_ZERO (&pool->fds_read);
    FD_ZERO (&pool->fds_write);
    FD_SET  (pool->ctl_recv, &pool->fds_read);
    pool->fd_max = pool->ctl_recv;
    pool->fd_min = pool->fd_max;

    // this, together with pthread_mutex_lock() in the beginning of
    // pool_thread() avoids possible race in access to pool->thread
    pthread_mutex_lock   (&pool->lock);
    ret = pthread_create (&pool->thread, NULL, pool_thread, pool);
    pthread_mutex_unlock (&pool->lock);
    if (ret) {
        perror ("Failed to create thread");
        return -ret;
    }

    return 0;
}

extern glb_pool_t*
glb_pool_create (size_t n_pools, glb_router_t* router)
{
    size_t ret_size = sizeof(glb_pool_t) + n_pools * sizeof(pool_t);
    glb_pool_t* ret = malloc (ret_size);

    if (ret) {
        long err;
        long i;

        memset (ret, 0, ret_size);
        pthread_mutex_init (&ret->lock, NULL);
        ret->n_pools = n_pools;

        for (i = 0; i < n_pools; i++) {
            if ((err = pool_init(&ret->pool[i], i, router))) {
                fprintf (stderr, "Failed to initialize pool %ld\n", i);
                abort();
            }
        }
    }
    else {
        fprintf (stderr, "Could not allocate memory for %zu pools\n", n_pools);
        abort();
    }

    gettimeofday (&ret->begin, NULL);

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

// Sends ctl and waits for confirmation from the pool thread
extern ssize_t
pool_send_ctl (pool_t* p, pool_ctl_t* ctl)
{
    ssize_t ret;

    pthread_mutex_lock (&p->lock);
    ret = write (p->ctl_send, ctl, sizeof (*ctl));
    if (ret != sizeof (*ctl)) {
        perror ("Sending ctl failed");
        if (ret > 0) abort(); // partial ctl was sent, don't know what to do
    }
    else ret = 0;
    pthread_cond_wait (&p->cond, &p->lock);
    pthread_mutex_unlock (&p->lock);

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
    void*   route = NULL;

    if (pthread_mutex_lock (&pool->lock)) {
        perror ("glb_pool_add_conn(): failed to lock mutex");
        abort();
    }

    route = malloc (pool_conn_size);
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

#ifdef GLB_USE_SPLICE
        if (pipe (inc_end->splice)) abort();
        if (pipe (dst_end->splice)) abort();
#endif
        ret = pool_send_ctl (p, &add_conn_ctl);
    }

    pthread_mutex_unlock (&pool->lock);

    return ret;
}

long
glb_pool_drop_dst (glb_pool_t* pool, const glb_sockaddr_t* dst)
{
    return 0;
}

size_t
glb_pool_print_stats (glb_pool_t* pool, char* buf, size_t buf_len)
{
    size_t len = 0;
    long i;
    struct timeval now;
    double         seconds;

#ifndef GLB_POOL_STATS
    len += snprintf (buf + len, buf_len - len, "Pool: connections per thread:");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }
#endif

    if (pthread_mutex_lock (&pool->lock)) {
        perror ("glb_pool_print_stats(): failed to lock mutex");
        abort();
    }

    gettimeofday (&now, NULL);
    seconds = now.tv_sec - pool->begin.tv_sec +
        (now.tv_usec - pool->begin.tv_usec) * 1.0e-06;

    for (i = 0; i < pool->n_pools; i++) {
#ifdef GLB_POOL_STATS
        pool_stats_t s = pool->pool[i].stats;

        pool->pool[i].stats = zero_stats;
        len += snprintf (buf + len, buf_len - len,
        "Pool %2ld: conns: %5ld, selects: %9zu (%9.2f sel/sec)\n"
        "recv   : %9zuB %9zuR %9zuS %9.2fB/R %9.2fB/sec %9.2fR/S %9.2fR/sec\n"
        "send   : %9zuB %9zuW %9zuS %9.2fB/W %9.2fB/sec %9.2fW/S %9.2fW/sec\n",
         i, pool->pool[i].n_conns, s.n_select, (double)s.n_select/seconds,
         s.recv_bytes,s.n_recv,s.sel_reads,(double)s.recv_bytes/s.n_recv,
         (double)s.recv_bytes/seconds,(double)s.n_recv/s.n_select,
         (double)s.n_recv/seconds,
         s.send_bytes,s.n_send,s.sel_writes,(double)s.send_bytes/s.n_send,
         (double)s.send_bytes/seconds,(double)s.n_send/s.n_select,
         (double)s.n_send/seconds
        );
        if (len == buf_len) {
            buf[len - 1] = '\0';
            return (len - 1);
        }
#else
        len += snprintf (buf + len, buf_len - len," %5ld",
                         pool->pool[i].n_conns);
        if (len == buf_len) {
            buf[len - 1] = '\0';
            return (len - 1);
        }
#endif
    }

    pthread_mutex_unlock (&pool->lock);

    len += snprintf (buf + len, buf_len - len,"\n");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    pool->begin = now;

    return len;
}
