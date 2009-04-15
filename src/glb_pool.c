/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#ifndef USE_EPOLL
    #ifndef USE_POLL
        #error "Neither USE_POLL nor USE_EPOLL defined!"
    #else
        #include <poll.h>
        typedef struct pollfd pollfd_t;
    #endif
#else
    #include <sys/epoll.h>
    typedef struct epoll_event pollfd_t;
#endif

#ifdef GLB_USE_SPLICE
#include <fcntl.h>
#endif

#include "glb_time.h"
#include "glb_log.h"
#include "glb_pool.h"

extern bool glb_verbose;

typedef enum pool_ctl_code
{
    POOL_CTL_ADD_CONN,
    POOL_CTL_DROP_DST,
    POOL_CTL_SHUTDOWN,
    POOL_CTL_STATS,
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
    int             fds_idx;  // index in the file descriptor set (for poll())
    uint32_t        events;   // events waited by descriptor
    size_t          sent;
    size_t          total;
    glb_sockaddr_t  dst_addr; // destinaiton id
#ifdef GLB_USE_SPLICE
    int             splice[2];
#endif
    uint8_t         buf[];    // has pool_buf_size
} pool_conn_end_t;

#define POOL_MAX_FD (1 << 16) // highest possible file descriptor + 1
                              // only affects the map size

// We want to allocate memory for both ends in one malloc() call and have it
// nicely aligned. This is presumably a page multiple and
// should be enough for two ethernet frames (what about jumbo?)
#define pool_end_size  (BUFSIZ)
#define pool_conn_size (pool_end_size << 2)
#define pool_buf_size  (pool_end_size - sizeof(pool_conn_end_t))

typedef struct pool
{
    long             id;
    pthread_t        thread;
    pthread_mutex_t  lock;
    pthread_cond_t   cond;
    int              ctl_recv; // fd to receive commands in pool thread
    int              ctl_send; // fd to send commands to pool - other function
    volatile ulong   n_conns;  // how many connecitons this pool serves
#ifdef USE_EPOLL
    int              epoll_fd;
#endif
    pollfd_t*        pollfds;
    size_t           pollfds_len;
    int              fd_max;
    glb_router_t*    router;
    glb_pool_stats_t stats;

    pool_conn_end_t* route_map[ POOL_MAX_FD ]; // connection ctx look-up by fd
} pool_t;

struct glb_pool
{
    pthread_mutex_t lock;
    ulong           n_pools;
    glb_time_t      begin;
    pool_t          pool[];  // pool array, can't be changed in runtime
};

typedef enum pool_fd_ops
{
#ifdef USE_EPOLL
    POOL_FD_READ  = EPOLLIN,
    POOL_FD_WRITE = EPOLLOUT,
#else // POLL
    POOL_FD_READ  = POLLIN,
    POOL_FD_WRITE = POLLOUT,
#endif // POLL
    POOL_FD_RW    = POOL_FD_READ | POOL_FD_WRITE
} pool_fd_ops_t;

//#define FD_SETSIZE 1024; // leater get it from select.h

static const pollfd_t zero_pollfd = { 0, };

/*!
 * @return negative error code or the index of file descriptor in the set
 */
static inline long
pool_fds_add (pool_t* pool, int fd, pool_fd_ops_t events)
{
    int ret;

    assert (fd > 0);
    assert (pool->fd_max <= pool->pollfds_len);

    if (pool->fd_max == pool->pollfds_len) { // allocate more memory
        void*  tmp;
        size_t tmp_len = pool->pollfds_len + FD_SETSIZE;

        tmp = realloc (pool->pollfds, tmp_len * sizeof(pollfd_t));
        if (NULL == tmp) {
            glb_log_fatal ("Failed to (re)allocate %d pollfds: out of memory",
                           tmp_len);
            return -ENOMEM;
        }

        memset (((pollfd_t*)tmp) + pool->fd_max, 0,
                (tmp_len - pool->fd_max) * sizeof(pollfd_t));

        pool->pollfds = tmp;
        pool->pollfds_len = tmp_len;        
    }

#ifdef USE_EPOLL
    struct epoll_event add_event = { .events = events, { .fd = fd }};

    ret = epoll_ctl (pool->epoll_fd, EPOLL_CTL_ADD, fd, &add_event);
    if (ret) {
        glb_log_error ("epoll_ctl (%d, EPOLL_CTL_ADD, %d, {%d, %llu}) failed: "
                       "%d (%s)",
                       pool->epoll_fd, fd, add_event.events, add_event.data.u64,
                       errno, strerror (errno));
        return -errno;
    }
#else // POLL
    pool->pollfds[pool->fd_max].fd = fd;        
    pool->pollfds[pool->fd_max].events = events;
#endif // POLL

    ret = pool->fd_max;

    pool->fd_max++; // track how many descriptors are there

    return ret;
}

// returns corresponding pool_conn_end_t*
static inline pool_conn_end_t*
pool_conn_end_by_fd (pool_t* pool, int fd)
{
    // map points to the other end, but that's enough
    register pool_conn_end_t* other_end = pool->route_map[fd];
    if (other_end->inc) {
        return (pool_conn_end_t*)((uint8_t*)other_end + pool_end_size);
    } else {
        return (pool_conn_end_t*)((uint8_t*)other_end - pool_end_size);
    }
}

// remove file descriptor from file descriptor set
static inline long
pool_fds_del (pool_t* pool, pool_conn_end_t* end)
{
    pool->fd_max--; // pool->fd_max is now the index of the last pollfd
    
#ifdef USE_EPOLL
    long ret = epoll_ctl (pool->epoll_fd, EPOLL_CTL_DEL, end->sock, NULL);
    if (ret) {
        glb_log_error ("epoll_ctl (%d, EPOLL_CTL_DEL, %d, NULL) failed: "
                       "%d (%s)",
                       pool->epoll_fd, end->sock, errno, strerror (errno));
        return -errno;
    }
#else // POLL
    assert (end->fds_idx <= pool->fd_max);

    /*
     * pay attention here: the last pollfd that we're moving may have not been
     * checked yet
     */

    // copy the last pollfd in place of the deleted
    pool->pollfds[end->fds_idx] = pool->pollfds[pool->fd_max];
    // from this pollfd find its fd and from route_map by fd find its
    // pool_coon_end struct and in that struct update fds_idx to point at
    // a new position.
    pool_conn_end_by_fd(pool, pool->pollfds[end->fds_idx].fd)->fds_idx =
        end->fds_idx;
    // zero-up the last pollfd
    pool->pollfds[pool->fd_max] = zero_pollfd;
#endif // POLL

    return 0;
}

static inline void
pool_fds_set_events (pool_t* pool, pool_conn_end_t* end)
{
#ifdef USE_EPOLL
    struct epoll_event event = { .events = end->events, { .fd = end->sock } };
    if (epoll_ctl (pool->epoll_fd, EPOLL_CTL_MOD, end->sock, &event)) {
        glb_log_fatal ("epoll_ctl(%d, EPOLL_CTL_MOD, %d, {%d, %llu}) failed: "
                       "%d (%s)",
                       pool->epoll_fd, end->sock, event.events, event.data.u64,
                       errno, strerror(errno));
        abort();
    }
#else // POLL
    pool->pollfds[end->fds_idx].events = end->events;
#endif // POLL
}

static inline long
pool_fds_wait (pool_t* pool)
{
#ifdef USE_EPOLL
    return epoll_wait (pool->epoll_fd, pool->pollfds, pool->fd_max, -1); 
#else // POLL
    return poll (pool->pollfds, pool->fd_max, -1);
#endif // POLL
}

// performs necessary magic (adds end-to-end mapping, alters fd_max and fd_min)
// when new file descriptor is added to fd_set
static inline void
pool_set_conn_end (pool_t* pool, pool_conn_end_t* end1, pool_conn_end_t* end2)
{
    end1->fds_idx = pool_fds_add (pool, end1->sock, POOL_FD_READ);
    if (end1->fds_idx < 0) abort();
    end1->events = POOL_FD_READ;
    pool->route_map[end1->sock] = end2;
}

// removing traces of connection end - reverse to what pool_set_conn_end() did
static inline void
pool_reset_conn_end (pool_t* pool, pool_conn_end_t* end)
{
    pool_fds_del (pool, end);
    close (end->sock);
    pool->route_map[end->sock] = NULL;
}

static void
pool_remove_conn (pool_t* pool, int src_fd, bool notify_router)
{
    pool_conn_end_t* dst    = pool->route_map[src_fd];
    int              dst_fd = dst->sock;
    pool_conn_end_t* src    = pool->route_map[dst_fd];

#ifndef NDEBUG
    if (notify_router && !src->inc) {
        glb_log_warn ("Connection close from server");
    }
#endif

    pool->n_conns--;

    if (notify_router)
        glb_router_disconnect (pool->router, &dst->dst_addr);

#ifdef GLB_USE_SPLICE
    close (dst->splice[0]); close (dst->splice[1]);
    close (src->splice[0]); close (src->splice[1]);
#endif

    // in reverse order to pool_set_conn_end() in pool_handle_add_conn()
    pool_reset_conn_end (pool, dst);
    pool_reset_conn_end (pool, src);

    if (dst->inc) {
        free (dst); // frees both ends
    }
    else {
        assert (src->inc);
        free (src); // frees both ends
    }
}

static void
pool_handle_add_conn (pool_t* pool, pool_ctl_t* ctl)
{
    pool_conn_end_t* inc_end = ctl->data;
    pool_conn_end_t* dst_end = ctl->data + pool_end_size;

    pool_set_conn_end (pool, inc_end, dst_end);
    pool_set_conn_end (pool, dst_end, inc_end);

    pool->n_conns++; // increment connection count
    if (glb_verbose) {
        glb_log_info ("Pool %ld: added connection, "
                      "(total pool connections: %ld)", pool->id, pool->n_conns);
    }
}

static void
pool_handle_drop_dst (pool_t* pool, pool_ctl_t* ctl)
{
    const glb_sockaddr_t* dst = ctl->data;
    int fd;
    int count = pool->fd_max - 1; // ctl_recv is not in route_map

    for (fd = 0; count; fd++) {
        pool_conn_end_t* end = pool->route_map[fd];

        assert (fd < POOL_MAX_FD);

        if (end) {
            count--;
            if (glb_socket_addr_is_equal(&end->dst_addr, dst)) {
                // remove conn, but don't try to notify router 'cause it's
                // already dropped this destination
                pool_remove_conn (pool, fd, false);
                count--; // 1 connection means 2 file descriptors
            }
        }
    }
}

static inline void
pool_handle_stats (pool_t* pool, pool_ctl_t* ctl)
{
    glb_pool_stats_t* stats = ctl->data;
    glb_pool_stats_add (stats, &pool->stats);
    pool->stats = glb_zero_stats;
}

#define GLB_MUTEX_LOCK(mtx)                                             \
{                                                                       \
    int ret;                                                            \
    if ((ret = pthread_mutex_lock (mtx))) {                             \
        glb_log_fatal ("Failed to lock mutex: %d (%s)", ret, strerror(ret));\
        abort();                                                        \
    }                                                                   \
}

#define GLB_MUTEX_UNLOCK(mtx)                                           \
{                                                                       \
    int ret;                                                            \
    if ((ret = pthread_mutex_unlock (mtx))) {                           \
        glb_log_fatal ("Failed to unlock mutex: %d (%s)", ret, strerror(ret));\
        abort();                                                        \
    }                                                                   \
}

static long
pool_handle_ctl (pool_t* pool)
{
    pool_ctl_t ctl;
    register long ret;

    // remove ctls from poll count to get only traffic polls
    pool->stats.n_polls--;

    ret = read (pool->ctl_recv, &ctl, sizeof(ctl));

    if (sizeof(ctl) != ret) { // incomplete ctl read, should never happen
        glb_log_fatal ("Incomplete read from ctl, errno: %d (%s)",
                       errno, strerror (errno));
        abort();
    }

    switch (ctl.code) {
    case POOL_CTL_ADD_CONN:
        pool_handle_add_conn (pool, &ctl);
        break;
    case POOL_CTL_DROP_DST:
        pool_handle_drop_dst (pool, &ctl);
        break;
    case POOL_CTL_STATS:
        pool_handle_stats    (pool, &ctl);
        break;
    default: // nothing else is implemented
        glb_log_warn ("Unsupported CTL: %d\n", ctl.code);
    }

    // Notify ctl sender
    GLB_MUTEX_LOCK (&pool->lock);
    pthread_cond_signal (&pool->cond);
    GLB_MUTEX_UNLOCK (&pool->lock);

    return 0;
}

static inline ssize_t
pool_send_data (pool_t* pool, pool_conn_end_t* dst, pool_conn_end_t* src)
{
    ssize_t  ret;
    uint32_t dst_events = dst->events;

#ifndef GLB_USE_SPLICE
    ret = send (dst->sock, &dst->buf[dst->sent], dst->total - dst->sent,
                MSG_DONTWAIT | MSG_NOSIGNAL);
#else
    ret = splice (dst->splice[0], NULL, dst->sock, NULL,
                  dst->total - dst->sent, SPLICE_F_NONBLOCK);
#endif

    if (ret > 0) {
        pool->stats.send_bytes += ret;

        dst->sent += ret;
        if (dst->sent == dst->total) {        // all data sent, reset pointers
            dst->sent  =  dst->total = 0;
            dst_events &= ~POOL_FD_WRITE;     // clear WRITE flag
        }
        else {                                // there is unsent data left
            glb_log_debug ("Setting WRITE flag on %s: sent = %zu, total = "
                           "%zu, bufsiz = %zu", dst->inc ? "client" : "server",
                           dst->sent, dst->total,pool_buf_size);
            dst_events |= POOL_FD_WRITE;      // set   WRITE flag
        }

        if (src && !(src->events & POOL_FD_READ) &&
            (dst->total < pool_buf_size)) {
            // some space exists in the buffer, reestablish READ flag in src
            src->events |= POOL_FD_READ;
            pool_fds_set_events (pool, src);
        }
    }
    else {
        ret = -errno;
        switch (-ret) {
        case ESPIPE:
        case EBUSY:
        case EINTR:
        case ENOBUFS:
        case EAGAIN:
            glb_log_debug ("Send data error: %d (%s)", -ret, strerror(-ret));
            dst_events |= POOL_FD_WRITE;
            ret = 0; // pretend nothing happened
            break;
        case EPIPE:
            glb_log_debug ("pool_remove_conn() from pool_send_data()");
            pool_remove_conn(pool, dst->sock, true);
            break;
        default:
            glb_log_warn ("Send data failed, unhandled error: %d (%s)",
                          -ret, strerror(-ret));
        }
    }

    pool->stats.n_send++;

    if (dst_events != dst->events) { // events changed
        glb_log_debug ("Old flags on %s: %s %s", dst->inc ? "client":"server",
                       dst->events & POOL_FD_READ ? "POOL_FD_READ":"",
                       dst->events & POOL_FD_WRITE ? "POOL_FD_WRITE":"");
        dst->events = dst_events;
        pool_fds_set_events (pool, dst);
        glb_log_debug ("New flags on %s: %s %s", dst->inc ? "client":"server",
                       dst->events & POOL_FD_READ ? "POOL_FD_READ":"",
                       dst->events & POOL_FD_WRITE ? "POOL_FD_WRITE":"");
    }

    return ret;
}

// inline because frequent
static inline ssize_t
pool_handle_read (pool_t* pool, int src_fd)
{
    ssize_t ret = 0;
    pool_conn_end_t* dst = pool->route_map[src_fd];

//    glb_log_debug ("pool_handle_read()");

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
            // now try to send whatever we have received so far
            // (since we're here, POOL_FD_READ on src is not cleared, no need
            // to set it once again)
            ssize_t send_err;
            if ((send_err = pool_send_data (pool, dst, NULL)) < 0) {
                glb_log_warn ("pool_send_data(): %zd (%s)",
                              -send_err, strerror(-send_err));
            }

            if (dst->total == pool_buf_size) {
                // no space for next read, clear POOL_FD_READ
                pool_conn_end_t* src = pool->route_map[dst->sock];
                assert (src->events & POOL_FD_READ);
                src->events &= ~POOL_FD_READ;
                pool_fds_set_events (pool, src);
            }

            pool->stats.recv_bytes += ret;
        }
        else {
            if (0 == ret) { // socket closed, must close another end and cleanup
//               glb_log_debug ("pool_remove_conn() from pool_handle_read()");
                pool_remove_conn (pool, src_fd, true);
                ret = -EPIPE;
            }
            else { // some other error
                if (errno != EAGAIN) {
                    ret = -errno;
                    glb_log_warn ("pool_handle_read(): %zd (%s)",
                                  -ret, strerror(-ret));
                }
            }
        }

        pool->stats.n_recv++;
    }
    return ret;
}

static inline ssize_t
pool_handle_write (pool_t* pool, int dst_fd)
{
    pool_conn_end_t* src = pool->route_map[dst_fd];
    pool_conn_end_t* dst = pool->route_map[src->sock];

    glb_log_debug ("pool_handle_write() to %s: %zu",
                   dst->inc ? "client" : "server", dst->total - dst->sent);

    if (dst->total) {
        ssize_t send_err;

        assert (dst->total > dst->sent);

        if ((send_err = pool_send_data (pool, dst, src)) < 0) {
            glb_log_warn ("pool_send_data(): %zd (%s)",
                          -send_err, strerror(-send_err));
        }
    }
    return 0;
}

// returns on error or after handling ctl - the latter may cause changes in
// file descriptors.
static inline long
pool_handle_events (pool_t* pool, long count)
{
    long idx;
#ifdef USE_EPOLL
    for (idx = 0; idx < count; idx++) {
        pollfd_t* pfd = pool->pollfds + idx;
        if (pfd->events & POOL_FD_READ) {

            if (pfd->data.fd != pool->ctl_recv) { // normal read
                register long ret;
                pool->stats.poll_reads++;

                ret = pool_handle_read (pool, pfd->data.fd);
                if (ret < 0) return ret;
            }
            else {                                // ctl read
                return pool_handle_ctl (pool);
            }
        }
        if (pfd->events & POOL_FD_WRITE) {
            register long ret;
            pool->stats.poll_writes++;

            assert (pfd->data.fd != pool->ctl_recv);
            ret = pool_handle_write (pool, pfd->data.fd);
            if (ret < 0) return ret;
        }
    }
#else // POLL
    if (pool->pollfds[0].revents & POOL_FD_READ) { // first, check ctl socket
        return pool_handle_ctl (pool);
    }

    for (idx = 1; count > 0; idx++)
    {
        pollfd_t* pfd = pool->pollfds + idx;

        assert (idx < pool->fd_max);

        if (pfd->revents) {
            // revents might be less than pfd->revents because some of the
            // pfd->events might be cleared in the previous loop
            register ulong revents = pfd->revents & pfd->events;

            if (revents & POOL_FD_READ) {
                register long ret;
                pool->stats.poll_reads++;

                ret = pool_handle_read (pool, pfd->fd);
                if (ret < 0) return ret;
            }
            if (revents & POOL_FD_WRITE) {
                register long ret;
                pool->stats.poll_writes++;

                long ret = pool_handle_write (pool, pfd->fd);
                if (ret < 0) return ret;
            }
            count--;
        }
    }
#endif // POLL
    return 0;
}

static void*
pool_thread (void* arg)
{
    pool_t* pool   = arg;

    // synchronize with the calling process
    GLB_MUTEX_LOCK (&pool->lock);
    GLB_MUTEX_UNLOCK (&pool->lock);

    while (1) {
        long ret;

        ret = pool_fds_wait (pool);

        if (ret > 0) {

            pool->stats.n_polls++;

            pool_handle_events (pool, ret);

        }
        else if (ret < 0) {
            glb_log_error ("pool_fds_wait() failed: %d (%s)",
                           errno, strerror(errno));
        }
        else {
            glb_log_error ("pool_fds_wait() interrupted: %d (%s)",
                           errno, strerror(errno));
        }
    }

    return NULL;
}

// initialize file descriptor set with ctl_recv descriptor
static long
pool_fds_init (pool_t* pool, int ctl_fd)
{
    pool->fd_max = 0;

#ifdef USE_EPOLL
    pool->epoll_fd = epoll_create(FD_SETSIZE);
    if (pool->epoll_fd < 0) {
        glb_log_fatal ("epoll_create(%d) failed: %d (%s)",
                       FD_SETSIZE, errno, strerror(errno));
        return -errno;
    }
#endif

    pool->pollfds_len = 0;

    return pool_fds_add (pool, ctl_fd, POOL_FD_READ);
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
        ret = errno;
        glb_log_fatal ("Failed to open control pipe: %d (%s)",
                       ret, strerror(ret));
        return -ret;
    }

    pool->ctl_recv = pipe_fds[0];
    pool->ctl_send = pipe_fds[1];

    ret = pool_fds_init (pool, pool->ctl_recv);
    if (ret < 0) {
        return ret;
    }

    // this, together with GLB_MUTEX_LOCK() in the beginning of
    // pool_thread() avoids possible race in access to pool->thread
    GLB_MUTEX_LOCK   (&pool->lock);
    ret = pthread_create (&pool->thread, NULL, pool_thread, pool);
    GLB_MUTEX_UNLOCK (&pool->lock);
    if (ret) {
        glb_log_fatal ("Failed to create pool thread: %d (%s)",
                       ret, strerror(ret));
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
                glb_log_fatal ("Failed to initialize pool %ld.", i);
                abort();
            }
        }
    }
    else {
        glb_log_fatal ("Could not allocate memory for %zu pools.",
                       n_pools);
        abort();
    }

    ret->begin = glb_time_now();

    return ret;
}

extern void
glb_pool_destroy (glb_pool_t* pool)
{
    long i;
    for (i = 0; i < pool->n_pools; i++) {
        pool_t* p = &pool->pool[i];
        pthread_join (p->thread, NULL);
    }
    // TODO: proper resource deallocation
    glb_log_warn ("glb_pool_destroy() not implemented yet!");
}

// finds the least busy pool
static inline pool_t*
pool_get_pool (glb_pool_t* pool)
{
    pool_t* ret       = pool->pool;
    ulong   min_conns = ret->n_conns;
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
static ssize_t
pool_send_ctl (pool_t* p, pool_ctl_t* ctl)
{
    ssize_t ret;

    GLB_MUTEX_LOCK (&p->lock);
    ret = write (p->ctl_send, ctl, sizeof (*ctl));
    if (ret != sizeof (*ctl)) {
        glb_log_error ("Sending ctl failed: %d (%s)", errno, strerror(errno));
        if (ret > 0) abort(); // partial ctl was sent, don't know what to do
    }
    else ret = 0;
    pthread_cond_wait (&p->cond, &p->lock);
    GLB_MUTEX_UNLOCK (&p->lock);

    return ret;
}

long
glb_pool_add_conn (glb_pool_t*     pool,
                   int             inc_sock,
                   int             dst_sock,
                   glb_sockaddr_t* dst_addr)
{
    pool_t* p     = pool_get_pool (pool);
    long    ret   = -ENOMEM;
    void*   route = NULL;

    GLB_MUTEX_LOCK (&pool->lock);

    route = malloc (pool_conn_size);
    if (route) {
        pool_conn_end_t* inc_end = route;
        pool_conn_end_t* dst_end = route + pool_end_size;
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

    GLB_MUTEX_UNLOCK (&pool->lock);

    return ret;
}

// returns 0 minus how many ctls failed
static inline long
pool_bcast_ctl (glb_pool_t* pool, pool_ctl_t* ctl)
{
    ulong i;
    long ret = 0;

    GLB_MUTEX_LOCK (&pool->lock);

    for (i = 0; i < pool->n_pools; i++) {
        ret -= (pool_send_ctl (&pool->pool[i], ctl) < 0);
    }

    GLB_MUTEX_UNLOCK (&pool->lock);

    return ret;
}

long
glb_pool_drop_dst (glb_pool_t* pool, const glb_sockaddr_t* dst)
{
    pool_ctl_t drop_dst_ctl = { POOL_CTL_DROP_DST, (void*)dst };
    return pool_bcast_ctl (pool, &drop_dst_ctl);
}

long
glb_pool_get_stats (glb_pool_t* pool, glb_pool_stats_t* stats)
{
    pool_ctl_t stats_ctl = { POOL_CTL_STATS, (void*)stats };
    return pool_bcast_ctl (pool, &stats_ctl);
}

size_t
glb_pool_print_stats (glb_pool_t* pool, char* buf, size_t buf_len)
{
    size_t     len = 0;
    long       i;
    glb_time_t now;
    double     elapsed;

#ifndef GLB_POOL_STATS
    len += snprintf (buf + len, buf_len - len, "Pool: connections per thread:");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }
#endif

    GLB_MUTEX_LOCK (&pool->lock);

    now     = glb_time_now ();
    elapsed = now - pool->begin;

    for (i = 0; i < pool->n_pools; i++) {
#ifdef GLB_POOL_STATS
        pool_stats_t s = pool->pool[i].stats;

        pool->pool[i].stats = zero_stats;

        len += snprintf (buf + len, buf_len - len,
        "Pool %2ld: conns: %5ld, selects: %9zu (%9.2f sel/sec)\n"
        "recv   : %9zuB %9zuR %9zuS %9.2fB/R %9.2fB/sec %9.2fR/S %9.2fR/sec\n"
        "send   : %9zuB %9zuW %9zuS %9.2fB/W %9.2fB/sec %9.2fW/S %9.2fW/sec\n",
         i, pool->pool[i].n_conns, s.n_polls, (double)s.n_polls/elapsed,
         s.recv_bytes,s.n_recv,s.poll_reads,(double)s.recv_bytes/s.n_recv,
         (double)s.recv_bytes/elapsed,(double)s.n_recv/s.n_polls,
         (double)s.n_recv/elapsed,
         s.send_bytes,s.n_send,s.poll_writes,(double)s.send_bytes/s.n_send,
         (double)s.send_bytes/elapsed,(double)s.n_send/s.n_polls,
         (double)s.n_send/elapsed
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

    GLB_MUTEX_UNLOCK (&pool->lock);

    len += snprintf (buf + len, buf_len - len,"\n");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    pool->begin = now;

    return len;
}
