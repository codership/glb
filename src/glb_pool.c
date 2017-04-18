/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id: glb_pool.c 160 2013-11-03 14:49:02Z alex $
 */

#include "glb_misc.h"
#include "glb_time.h"
#include "glb_log.h"
#include "glb_pool.h"

#include "glb_cmd.h"
#include "glb_types.h" // ulong

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>

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

typedef enum pool_ctl_code
{
    POOL_CTL_ADD_CONN,
    POOL_CTL_DROP_DST,
    POOL_CTL_STATS,
    POOL_CTL_SHUTDOWN,
    POOL_CTL_MAX
} pool_ctl_code_t;

typedef struct pool_ctl
{
    pool_ctl_code_t code;
    void*           data;
} pool_ctl_t;

// connection end can be either client or server
// and server end can be either complete or incomplete
typedef enum pool_end
{
    POOL_END_INCOMPLETE = 0,
    POOL_END_COMPLETE,
    POOL_END_CLIENT
} pool_end_t;

typedef struct pool_conn_end
{
    glb_sockaddr_t addr;
    size_t         sent;
    size_t         total;
#ifdef GLB_USE_SPLICE
    int            splice[2];
#endif
    int            sock;     // fd of connection
    int            fds_idx;  // index in the file descriptor set (for poll())
    uint32_t       events;   // events waited by descriptor
    pool_end_t     end;      // to differentiate between the ends
    uint8_t        buf[];    // has pool_buf_size
} pool_conn_end_t;

/* We want to allocate memory for both ends in one malloc() call and have it
 * nicely aligned. This is presumably a page multiple and should be enough
 * for two ethernet frames (what about jumbo?) */
/* Overall layout: |inc end|....inc buf....|dst end|....dst buf....| */
#define pool_end_size  BUFSIZ
#define pool_conn_size (pool_end_size << 1) // total 2 buffers
#define pool_buf_size  (pool_end_size - sizeof(pool_conn_end_t))

#define POOL_MAX_FD (1 << 16) // highest possible file descriptor + 1
                              // only affects the map size

typedef struct pool
{
    const glb_cnf_t* cnf;
    glb_sockaddr_t   addr_out; // for async conn handling.
    pthread_t        thread;
    pthread_mutex_t  lock;
    pthread_cond_t   cond;
    int              id;
    int              ctl_recv; // fd to receive commands in pool thread
    int              ctl_send; // fd to send commands to pool - other function
    volatile int     n_conns;  // how many connecitons this pool serves
#ifdef USE_EPOLL
    int              epoll_fd;
#endif
    pollfd_t*        pollfds;
    size_t           pollfds_len;
    int              fd_max;
    glb_router_t*    router;
    glb_pool_stats_t stats;
    bool             shutdown;
    pool_conn_end_t* route_map[ POOL_MAX_FD ]; // connection ctx look-up by fd
} pool_t;

struct glb_pool
{
    const glb_cnf_t* cnf;
    pthread_mutex_t lock;
    glb_time_t      last_info;
    glb_time_t      last_stats;
    int             n_pools;
    pool_t          pool[];  // pool array, can't be changed in runtime
};

typedef enum pool_fd_ops
{
#ifdef USE_EPOLL
    POOL_FD_READ  = EPOLLIN,
    POOL_FD_WRITE = EPOLLOUT | EPOLLERR,
#else /* POLL */
    POOL_FD_READ  = POLLIN,
    POOL_FD_WRITE = POLLOUT  | POLLERR,
#endif /* POLL */
    POOL_FD_RW    = POOL_FD_READ | POOL_FD_WRITE
} pool_fd_ops_t;

//#define FD_SETSIZE 1024; // leater get it from select.h

#ifndef USE_EPOLL
static const pollfd_t zero_pollfd = { 0, };
#endif /* !EPOLL */

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
#else /* POLL */
    pool->pollfds[pool->fd_max].fd = fd;
    pool->pollfds[pool->fd_max].events = events;
#endif /* POLL */

    ret = pool->fd_max;

    pool->fd_max++; // track how many descriptors are there

    return ret;
}

// returns corresponding pool_conn_end_t*
static inline pool_conn_end_t*
pool_conn_end_by_fd (pool_t* pool, int fd)
{
    // map points to the other end, but that's enough
    pool_conn_end_t* other_end = pool->route_map[fd];
    if (POOL_END_CLIENT == other_end->end) {
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
#else /* POLL */
    assert (end->fds_idx <= pool->fd_max);

    /*
     * pay attention here: the last pollfd that we're moving may have not been
     * checked yet
     */

    // copy the last pollfd in place of the deleted
    pool->pollfds[end->fds_idx] = pool->pollfds[pool->fd_max];
    // from this pollfd find its fd and from route_map by fd find its
    // pool_conn_end struct and in that struct update fds_idx to point at
    // a new position.
    pool_conn_end_by_fd(pool, pool->pollfds[end->fds_idx].fd)->fds_idx =
        end->fds_idx;
    // zero-up the last pollfd
    pool->pollfds[pool->fd_max] = zero_pollfd;
#endif /* POLL */

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
#else /* POLL */
    pool->pollfds[end->fds_idx].events = end->events;
#endif /* POLL */
}

static inline int
pool_fds_wait (pool_t* pool)
{
#ifdef USE_EPOLL
    return epoll_wait (pool->epoll_fd, pool->pollfds, pool->fd_max, -1);
#else /* POLL */
    return poll (pool->pollfds, pool->fd_max, -1);
#endif /* POLL */
}

// performs necessary magic (adds end-to-end mapping, alters fd_max and fd_min)
// when new file descriptor is added to fd_set
static inline void
pool_set_conn_end (pool_t* pool, pool_conn_end_t* end1, pool_conn_end_t* end2)
{
    pool_fd_ops_t event =
        POOL_END_INCOMPLETE == end1->end ? POOL_FD_WRITE : POOL_FD_READ;

    end1->fds_idx = pool_fds_add (pool, end1->sock, event);
    if (end1->fds_idx < 0) abort();

    end1->events = event;
    pool->route_map[end1->sock] = end2;
}

// removing traces of connection end - reverse to what pool_set_conn_end() did
static inline void
pool_reset_conn_end (pool_t* const pool, pool_conn_end_t* const end,
                     bool const cl)
{
    pool_fds_del (pool, end);
    if (cl) close (end->sock);
    pool->route_map[end->sock] = NULL;
}

static void
pool_remove_conn (pool_t* const pool, int const fd, bool const notify_router)
{
    pool_conn_end_t* inc_end;
    pool_conn_end_t* dst_end;
    bool             full; // whether to do full cleanup

    if (pool->route_map[fd]->end != POOL_END_CLIENT) { // close from client
        dst_end = pool->route_map[fd];
        inc_end = (pool_conn_end_t*)(((uint8_t*)dst_end) - pool_end_size);
        full    = true;
    }
    else {
        inc_end = pool->route_map[fd];
        dst_end = (pool_conn_end_t*)(((uint8_t*)inc_end) + pool_end_size);
        full    = POOL_END_INCOMPLETE != dst_end->end;

#ifndef NDEBUG
        if (notify_router) { glb_log_warn ("Connection close from server"); }
#endif
    }

    pool->n_conns--;
    pool->stats.conns_closed++;
    pool_reset_conn_end (pool, dst_end, true);

    if (full) {
        pool_reset_conn_end (pool, inc_end, true);

        if (notify_router)
            glb_router_disconnect (pool->router, &dst_end->addr, false);

#ifdef GLB_USE_SPLICE
        close (dst_end->splice[0]); close (dst_end->splice[1]);
        close (inc_end->splice[0]); close (inc_end->splice[1]);
#endif
        free (inc_end); // frees both ends
    }
    else {
        pool_reset_conn_end (pool, inc_end, false);
        assert (false == notify_router);
        /* at this point we should be as if before pool_handle_add_conn() */
    }
}

static inline int
pool_handle_async_conn (pool_t*          pool,
                        pool_conn_end_t* dst_end)
{
    uint32_t const ka_opt = pool->cnf->keepalive * GLB_SOCK_KEEPALIVE;

    dst_end->sock = glb_socket_create(&pool->addr_out,
                                      GLB_SOCK_NODELAY  |
                                      GLB_SOCK_NONBLOCK |
                                      ka_opt);
    int error;
    if (dst_end->sock > 0) {
        int ret = connect (dst_end->sock, (struct sockaddr*)&dst_end->addr,
                           sizeof (dst_end->addr));
        error = ret ? errno : 0;
        assert (error); /* should be EINPROGRESS in case of success */
        if (GLB_UNLIKELY(error != EINPROGRESS) && error != 0) {
            glb_log_error ("Async connect() failed: %d (%s)",
                           error, strerror(error));
            close (dst_end->sock);
        }
        else error = 0;
    }
    else {
        error = errno;
        if (error != EINPROGRESS) {
            glb_log_error ("Creating destination socket failed: %d (%s)",
                           error, strerror(error));
        }
    }

    return error;
}

static void
pool_handle_add_conn (pool_t* pool, pool_ctl_t* ctl)
{
    pool_conn_end_t* inc_end = ctl->data;
    pool_conn_end_t* dst_end = ctl->data + pool_end_size;

    assert (POOL_END_CLIENT == inc_end->end);
    assert (inc_end->sock > 0);

    if (dst_end->sock < 0) {
        assert (POOL_END_INCOMPLETE == dst_end->end);
        if (pool_handle_async_conn (pool, dst_end)) {
            glb_router_disconnect (pool->router, &dst_end->addr, true);
            close (inc_end->sock);
            free (inc_end);
            return;
        }
    }
    else {
        assert (POOL_END_COMPLETE   == dst_end->end);
    }

    pool_set_conn_end (pool, inc_end, dst_end);
    pool_set_conn_end (pool, dst_end, inc_end);

    pool->n_conns++;
    pool->stats.conns_opened++;

    if (pool->cnf->verbose) {
        glb_log_info ("Pool %d: added connection "
                      "(total pool connections: %d)", pool->id, pool->n_conns);
    }
}

static inline const glb_sockaddr_t*
pool_conn_end_dstaddr (pool_conn_end_t* end)
{
    const glb_sockaddr_t* const ret =
        ((glb_sockaddr_t*)(POOL_END_CLIENT == end->end ?
                           (uint8_t*)end : ((uint8_t*)end) - pool_end_size)) -1;
    return ret;
}

static void
pool_handle_drop_dst (pool_t* pool, pool_ctl_t* ctl)
{
    assert (POOL_CTL_DROP_DST == ctl->code);

    const glb_sockaddr_t* const dst = ctl->data;
    int fd;
    int count = pool->fd_max - 1; // ctl_recv is not in route_map

    for (fd = 0; count; fd++) {
        pool_conn_end_t* const end = pool->route_map[fd];

        assert (fd < POOL_MAX_FD);

        if (end) {
            count--;
            const glb_sockaddr_t* const end_dst = pool_conn_end_dstaddr (end);
            if (glb_sockaddr_is_equal (dst, end_dst)) {
                // remove conn, but don't try to notify router 'cause it's
                // already dropped this destination
                pool_remove_conn (pool, fd, false);
                count--; // removing connection removes 2 file descriptors
            }
        }
    }
}

static inline void
pool_handle_stats (pool_t* pool, pool_ctl_t* ctl)
{
    glb_pool_stats_t* stats = ctl->data;
    pool->stats.n_conns = pool->n_conns;
    glb_pool_stats_add (stats, &pool->stats);
    pool->stats = glb_zero_stats;
}

static void
pool_handle_shutdown (pool_t* pool)
{
    int fd;
    int count = pool->fd_max - 1; // ctl_recv is not in route_map

    for (fd = 0; count; fd++) {
        pool_conn_end_t* end = pool->route_map[fd];

        assert (fd < POOL_MAX_FD);

        if (end) {
            pool_remove_conn (pool, fd, false);
            count -= 2;
        }
    }

    close (pool->ctl_recv);
    pool->shutdown = true;
}

static int
pool_handle_ctl (pool_t* pool)
{
    pool_ctl_t ctl;

    // remove ctls from poll count to get only traffic polls
    pool->stats.n_polls--;

    ssize_t ret = read (pool->ctl_recv, &ctl, sizeof(ctl));

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
    case POOL_CTL_SHUTDOWN:
        pool_handle_shutdown (pool);
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
#ifdef MSG_NOSIGNAL
    ret = send (dst->sock, &dst->buf[dst->sent], dst->total - dst->sent,
                MSG_DONTWAIT | MSG_NOSIGNAL);
#else
    ret = send (dst->sock, &dst->buf[dst->sent], dst->total - dst->sent,
                MSG_DONTWAIT);
#endif
#else
    ret = splice (dst->splice[0], NULL, dst->sock, NULL,
                  dst->total - dst->sent, SPLICE_F_NONBLOCK);
#endif

    if (GLB_LIKELY(ret > 0)) {
        pool->stats.send_bytes += ret;
        pool->stats.tx_bytes   += (POOL_END_CLIENT == dst->end) * ret;

        dst->sent += ret;
        if (dst->sent == dst->total) {        // all data sent, reset pointers
            dst->sent  =  dst->total = 0;
            dst_events &= ~POOL_FD_WRITE;     // clear WRITE flag
        }
        else {                                // there is unsent data left
            glb_log_debug ("Setting WRITE flag on %s: sent = %zu, total = "
                           "%zu, bufsiz = %zu",
                           POOL_END_CLIENT == dst->end ? "client" : "server",
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
        case EINTR:    // 4
        case EAGAIN:   // 11
        case EBUSY:    // 16
        case ENOBUFS:  // 105
        case ENOTCONN: // 107
            glb_log_debug ("Send data error: %d (%s)", -ret, strerror(-ret));
            dst_events |= POOL_FD_WRITE;
            ret = 0; // pretend nothing happened
            break;
        case EPIPE:    // 32
            /* according to man 2 send,
             * Linux can return EPIPE on unconnected socket. WTF? */
            if (POOL_END_INCOMPLETE != dst->end) {
                glb_log_debug ("pool_remove_conn() from pool_send_data()");
                pool_remove_conn(pool, dst->sock, true);
            }
            else {
                /* POOL_FD_WRITE is already set there, just wait for it */
                ret = 0;
            }
            return ret;
        default:
            glb_log_warn ("Send data failed, unhandled error: %d (%s)",
                          -ret, strerror(-ret));
        }
    }

    pool->stats.n_send++;

    if (dst_events != dst->events) { // events changed
        glb_log_debug ("Old flags on %s: %s %s",
                       POOL_END_CLIENT == dst->end ? "client":"server",
                       dst->events & POOL_FD_READ  ? "POOL_FD_READ":"",
                       dst->events & POOL_FD_WRITE ? "POOL_FD_WRITE":"");
        dst->events = dst_events;
        pool_fds_set_events (pool, dst);
        glb_log_debug ("New flags on %s: %s %s",
                       POOL_END_CLIENT == dst->end ? "client":"server",
                       dst->events & POOL_FD_READ  ? "POOL_FD_READ":"",
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
        if (GLB_LIKELY(ret > 0)) {
            dst->total += ret;
            // now try to send whatever we have received so far
            // (since we're here, POOL_FD_READ on src is not cleared, no need
            // to set it once again)
            ssize_t send_err;
            if ((send_err = pool_send_data (pool, dst, NULL)) < 0) {
                glb_log_warn ("pool_send_data(): %zd (%s)",
                              -send_err, strerror(-send_err));
                if (-EPIPE == send_err) {
                    /* connection removed in pool_send_data(), nothing to do */
                    return send_err;
                }
            }

            if (dst->total == pool_buf_size) {
                // no space for next read, clear POOL_FD_READ
                pool_conn_end_t* src = pool->route_map[dst->sock];
                assert (src->events & POOL_FD_READ);
                src->events &= ~POOL_FD_READ;
                pool_fds_set_events (pool, src);
            }

            pool->stats.recv_bytes += ret;

            // increment only if it's coming from incoming interface
            pool->stats.rx_bytes   += (POOL_END_CLIENT != dst->end) * ret;
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
                    if (errno != ECONNRESET || pool->cnf->verbose) {
                        glb_log_warn ("pool_handle_read(): %zd (%s)",
                                      errno, strerror(errno));
                    }
                }
            }
        }

        pool->stats.n_recv++;
    }
    return ret;
}

static int
pool_handle_conn_complete (pool_t* pool, pool_conn_end_t* dst_end)
{
    int ret = -1;
    socklen_t ret_size = sizeof(ret);

    assert (POOL_END_INCOMPLETE == dst_end->end);

    getsockopt (dst_end->sock, SOL_SOCKET, SO_ERROR, &ret, &ret_size);

    if (ret) {
        glb_sockaddr_str_t a = glb_sockaddr_to_str (&dst_end->addr);
        glb_log_info ("Async connection to %s failed: %d (%s)",
                      a.str, ret, strerror (ret));

        pool_conn_end_t* const inc_end =
            (pool_conn_end_t*)(((uint8_t*)dst_end) - pool_end_size);

        uint32_t const hint = pool->cnf->policy < GLB_POLICY_SOURCE ?
            0 : glb_sockaddr_hash (&inc_end->addr);

        int err = glb_router_choose_dst_again (pool->router, hint, &dst_end->addr);

        if (!err) {
            a = glb_sockaddr_to_str (&dst_end->addr);
            glb_log_info ("Reconnecting to %s", a.str);
            pool_remove_conn (pool, dst_end->sock, false); dst_end->sock = -1;

            pool_ctl_t ctl = { POOL_CTL_ADD_CONN, inc_end };
            pool_handle_add_conn (pool, &ctl);
        }
        else {
            dst_end->end = POOL_END_COMPLETE; // cause complete cleanup
            pool_remove_conn (pool, dst_end->sock, false/* router didn't give us
                                                         * any destination */);
        }
    }
    else {
        dst_end->end = POOL_END_COMPLETE;
        dst_end->events = POOL_FD_READ;
        pool_fds_set_events (pool, dst_end);
    }

    return ret;
}

static inline int
pool_handle_write (pool_t* pool, int dst_fd)
{
    pool_conn_end_t* src = pool->route_map[dst_fd];
    pool_conn_end_t* dst = pool->route_map[src->sock];

    if (pool->cnf->verbose) {
        glb_log_debug ("pool_handle_write() to %s: %zu",
                       POOL_END_CLIENT == dst->end ? "client" : "server",
                       dst->total - dst->sent);
    }

    if (GLB_UNLIKELY(POOL_END_INCOMPLETE == dst->end) &&
        pool_handle_conn_complete (pool, dst)) return 0;

    assert (dst->end != POOL_END_INCOMPLETE);

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
static inline int
pool_handle_events (pool_t* pool, int count)
{
    int idx;
#ifdef USE_EPOLL
    for (idx = 0; idx < count; idx++) {
        pollfd_t* pfd = pool->pollfds + idx;
        if (pfd->events & POOL_FD_READ) {

            if (GLB_LIKELY(pfd->data.fd != pool->ctl_recv)) { // normal read
                ssize_t ret;
                pool->stats.poll_reads++;

                ret = pool_handle_read (pool, pfd->data.fd);
                if (ret < 0) return ret;
            }
            else {                                // ctl read
                return pool_handle_ctl (pool);
            }
        }
        if (pfd->events & POOL_FD_WRITE) {
            int ret;
            pool->stats.poll_writes++;

            assert (pfd->data.fd != pool->ctl_recv);
            ret = pool_handle_write (pool, pfd->data.fd);
            if (ret < 0) return ret;
        }
    }
#else /* POLL */
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
            ulong revents = pfd->revents & pfd->events;

            if (revents & POOL_FD_READ) {
                ssize_t ret;
                pool->stats.poll_reads++;

                ret = pool_handle_read (pool, pfd->fd);
                if (ret < 0) return ret;
            }
            if (revents & POOL_FD_WRITE) {
                int ret;
                pool->stats.poll_writes++;

                ret = pool_handle_write (pool, pfd->fd);
                if (ret < 0) return ret;
            }
            count--;
        }
    }
#endif /* POLL */
    return 0;
}

static void*
pool_thread (void* arg)
{
    pool_t* pool   = arg;

    // synchronize with the calling process
    GLB_MUTEX_LOCK (&pool->lock);
    GLB_MUTEX_UNLOCK (&pool->lock);

    while (!pool->shutdown) {
        int ret;

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

    glb_log_debug ("Pool %d thread exiting.", pool->id);

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

static void
pool_fds_release (pool_t* pool)
{
    free (pool->pollfds);
#ifdef USE_EPOLL
    close (pool->epoll_fd);
#endif /* USE_EPOLL */
}

static int
pool_init (const glb_cnf_t* cnf, pool_t* pool, long id, glb_router_t* router)
{
    int ret;
    int pipe_fds[2];

    pool->cnf    = cnf;
    pool->id     = id;
    pool->router = router;
    pool->stats  = glb_zero_stats;

    glb_sockaddr_init  (&pool->addr_out, "0.0.0.0", 0); //for outgoing conn
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

glb_pool_t*
glb_pool_create (const glb_cnf_t* cnf, glb_router_t* router)
{
    size_t ret_size = sizeof(glb_pool_t) + cnf->n_threads * sizeof(pool_t);
    glb_pool_t* ret = malloc (ret_size);

    if (ret) {
        int err;
        int i;

        memset (ret, 0, ret_size);
        pthread_mutex_init (&ret->lock, NULL);
        ret->cnf     = cnf;
        ret->n_pools = ret->cnf->n_threads;

        for (i = 0; i < ret->n_pools; i++) {
            if ((err = pool_init(ret->cnf, &ret->pool[i], i, router))) {
                glb_log_fatal ("Failed to initialize pool %d.", i);
                abort();
            }
        }
    }
    else {
        glb_log_fatal ("Could not allocate memory for %zu pools.",
                       cnf->n_threads);
        abort();
    }

    ret->last_info  = glb_time_now();
    ret->last_stats = ret->last_info;

    return ret;
}

// finds the least busy pool
static inline pool_t*
pool_get_pool (glb_pool_t* pool)
{
    pool_t* ret       = pool->pool;
    int     min_conns = ret->n_conns;
    int     i;

    for (i = 1; i < pool->n_pools; i++) {
        if (min_conns > pool->pool[i].n_conns) {
            min_conns = pool->pool[i].n_conns;
            ret = pool->pool + i;
        }
    }
    return ret;
}

// Sends ctl and waits for confirmation from the pool thread
static int
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

int
glb_pool_add_conn (glb_pool_t*           const pool,
                   int                   const inc_sock,
                   const glb_sockaddr_t* const inc_addr,
                   int                   const dst_sock,
                   const glb_sockaddr_t* const dst_addr,
                   bool                  const complete)
{
    int   ret   = -ENOMEM;
    void* route = NULL;

    route = malloc (pool_conn_size);
    if (route) {
        pool_conn_end_t* const inc_end = route;
        pool_conn_end_t* const dst_end = route + pool_end_size;

        inc_end->addr     = *inc_addr;
        inc_end->end      = POOL_END_CLIENT;
        inc_end->sock     = inc_sock;
        inc_end->sent     = 0;
        inc_end->total    = 0;

        dst_end->addr     = *dst_addr;
        dst_end->end      = complete ? POOL_END_COMPLETE : POOL_END_INCOMPLETE;
        dst_end->sock     = dst_sock;
        dst_end->sent     = 0;
        dst_end->total    = 0;

#ifdef GLB_USE_SPLICE
        if (pipe (inc_end->splice)) abort();
        if (pipe (dst_end->splice)) abort();
#endif
        pool_ctl_t add_conn_ctl = { POOL_CTL_ADD_CONN, inc_end };

        GLB_MUTEX_LOCK (&pool->lock);
        pool_t* p = pool_get_pool (pool);
        GLB_MUTEX_UNLOCK (&pool->lock);

        ret = pool_send_ctl (p, &add_conn_ctl);
    }

    return ret;
}

// Sends the same ctl to all pools. Returns 0 minus how many ctls failed
static inline int
pool_bcast_ctl (glb_pool_t* pool, pool_ctl_t* ctl)
{
    int i;
    int ret = 0;

    GLB_MUTEX_LOCK (&pool->lock);

    for (i = 0; i < pool->n_pools; i++) {
        ret -= (pool_send_ctl (&pool->pool[i], ctl) < 0);
    }

    GLB_MUTEX_UNLOCK (&pool->lock);

    return ret;
}

int
glb_pool_drop_dst (glb_pool_t* pool, const glb_sockaddr_t* dst)
{
    pool_ctl_t drop_dst_ctl = { POOL_CTL_DROP_DST, (void*)dst };
    return pool_bcast_ctl (pool, &drop_dst_ctl);
}

ssize_t
glb_pool_print_stats (glb_pool_t* pool, char* buf, size_t buf_len)
{
    glb_pool_stats_t stats = glb_zero_stats;
    pool_ctl_t stats_ctl   = { POOL_CTL_STATS, (void*)&stats };
    ssize_t    ret;
    glb_time_t now = glb_time_now();

    ret = pool_bcast_ctl (pool, &stats_ctl);
    if (!ret) {
        double elapsed = glb_time_seconds(now - pool->last_stats);
        ret = snprintf (buf, buf_len, "in: %lu out: %lu "
                        "recv: %lu / %lu send: %lu / %lu "
                        "conns: %lu / %lu poll: %lu / %lu / %lu "
                        "elapsed: %.5f\n",
                        stats.rx_bytes, stats.tx_bytes,
                        stats.recv_bytes, stats.n_recv,
                        stats.send_bytes, stats.n_send,
                        stats.conns_opened, stats.n_conns,
                        stats.poll_reads, stats.poll_writes, stats.n_polls,
                        elapsed);
    }
    else {
        assert (ret < 0);
        glb_log_error ("Failed to get stats from %d thread pools.", ret);
    }

    pool->last_stats = now;

    return ret;
}

ssize_t
glb_pool_print_info (glb_pool_t* pool, char* buf, size_t buf_len)
{
    size_t     len = 0;

#ifndef GLB_POOL_STATS
    len += snprintf (buf + len, buf_len - len, "Pool: connections per thread:");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }
#endif

    GLB_MUTEX_LOCK (&pool->lock);

#ifdef GLB_POOL_STATS
    {
    glb_time_t now = glb_time_now ();
    double elapsed = glb_time_seconds (now - pool->last_info);
#endif

    int i;
    for (i = 0; i < pool->n_pools; i++) {
#ifdef GLB_POOL_STATS
        glb_pool_stats_t s = pool->pool[i].stats;

        pool->pool[i].stats = glb_zero_stats;

        len += snprintf (buf + len, buf_len - len,
        "Pool %2d: conns: %5d, selects: %9zu (%9.2f sel/sec)\n"
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
            GLB_MUTEX_UNLOCK (&pool->lock);
            return (len - 1);
        }
#else
        len += snprintf (buf + len, buf_len - len," %5d",pool->pool[i].n_conns);
        if (len == buf_len) {
            buf[len - 1] = '\0';
            GLB_MUTEX_UNLOCK (&pool->lock);
            return (len - 1);
        }
#endif
    }

#ifdef GLB_POOL_STATS
    pool->last_info = now;
    }
#endif

    GLB_MUTEX_UNLOCK (&pool->lock);

    len += snprintf (buf + len, buf_len - len,"\n");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    return len;
}

void
glb_pool_destroy (glb_pool_t* pool)
{
    long i;
    pool_ctl_t shutdown_ctl = { POOL_CTL_SHUTDOWN, NULL };
    int err = pool_bcast_ctl (pool, &shutdown_ctl);

    if (err) glb_log_debug ("shutdown broadcast failed: %d", -err);

    for (i = 0; i < pool->n_pools; i++) {
        pool_t* p = &pool->pool[i];
        pthread_join (p->thread, NULL);
        close (p->ctl_send);
        pool_fds_release (p);
        pthread_cond_destroy  (&p->cond);
        pthread_mutex_destroy (&p->lock);
    }

    pthread_mutex_destroy (&pool->lock);
    free (pool);
}


