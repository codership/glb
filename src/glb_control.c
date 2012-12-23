/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

// keep asserts here for now
#undef NDEBUG

#include "glb_log.h"
#include "glb_limits.h"
#include "glb_signal.h"
#include "glb_control.h"

#include "glb_cmd.h"

#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

// unfortunately I see no way to use glb_pool.c polling code in here
// so it is yet another implementation
#include <poll.h>

typedef struct pollfd pollfd_t;

static const char ctrl_getinfo_cmd[] = "getinfo";
static const char ctrl_getstat_cmd[] = "getstat";

#if 0
typedef enum ctrl_fd
{
    CTRL_FIFO = 0,
    CTRL_LISTEN,
} ctrl_fd_t;
#endif
#define CTRL_MAX GLB_MAX_CTRL_CONN

struct glb_ctrl
{
    glb_cnf_t*    cnf;
    glb_router_t* router;
    glb_pool_t*   pool;
    glb_wdog_t*   wdog;
    pthread_t     thread;
    int           fifo;
    int           inet_sock;
    int const     inet_fd;
    int           fd_max;
    pollfd_t      fds[CTRL_MAX];
    uint16_t      default_port;
};

static void
ctrl_add_client (glb_ctrl_t* ctrl, int fd)
{
    assert (ctrl->fd_max < CTRL_MAX);

    ctrl->fds[ctrl->fd_max].fd      = fd;
    ctrl->fds[ctrl->fd_max].events  = POLLIN;
    ctrl->fds[ctrl->fd_max].revents = 0;

    ctrl->fd_max++;

    if (CTRL_MAX == ctrl->fd_max) // no more clients allowed
        ctrl->fds[ctrl->inet_fd].events = 0;
}

static void
ctrl_del_client (glb_ctrl_t* ctrl, int fd)
{
    int idx;

    assert (CTRL_MAX >= ctrl->fd_max);
    assert (ctrl->fd_max > 1);

    for (idx = 1; idx < ctrl->fd_max; idx++) {
        if (fd == ctrl->fds[idx].fd) {
            close (ctrl->fds[idx].fd);
            ctrl->fd_max--;
            ctrl->fds[idx] = ctrl->fds[ctrl->fd_max];
            ctrl->fds[ctrl->fd_max].fd      = 0;
            ctrl->fds[ctrl->fd_max].events  = 0;
            ctrl->fds[ctrl->fd_max].revents = 0;
            break;
        }
    }

    if (!(ctrl->fd_max < CTRL_MAX)) {
        glb_log_fatal ("Failed to cleanup control connection.");
        abort();
    };

    ctrl->fds[ctrl->inet_fd].events = POLLIN;
}

static inline void
ctrl_respond (glb_ctrl_t* ctrl, int fd, const char* resp)
{
    if (fd != ctrl->fifo) {
        // can't respond to FIFO, as will immediately read it back
        if (write (fd, resp, strlen(resp)) < strlen(resp))
            glb_log_error ("Failed to respond to control message: %d (%s)",
                           errno, strerror(errno));
    }
}

static int
ctrl_handle_request (glb_ctrl_t* ctrl, int fd)
{
    char    req[BUFSIZ];
    size_t  req_size = 0;
    ssize_t ret;

    memset (req, '\0', BUFSIZ); // just in case
    do {
        ret = read (fd, req + req_size, BUFSIZ);
        if (ret > 0) req_size += ret;
    }
    while (EINTR == errno);

    if (0 == req_size || ret < 0) { // connection closed or reset
        ctrl_del_client (ctrl, fd);
        return ret;
    }

    assert (req_size > 0);

    // Cut any trailing crap (newlines, whitespace etc.)
    for (ret = strlen(req) - 1; ret >= 0; ret--) {
        if (!(isalnum(req[ret]) || ispunct(req[ret]))) {
            req[ret] = '\0';
        } else {
            break; // stop at first alnum or punct character
        }
    }

    if (!strncasecmp (ctrl_getinfo_cmd, req, strlen(ctrl_getinfo_cmd))) {
        glb_router_print_info (ctrl->router, req, sizeof(req));
        ctrl_respond (ctrl, fd, req);
        return 0;
    }
    else if (ctrl->pool &&
             !strncasecmp (ctrl_getstat_cmd, req, strlen(ctrl_getstat_cmd))) {
        glb_pool_print_stats (ctrl->pool, req, sizeof(req));
        ctrl_respond (ctrl, fd, req);
        return 0;
    }
    else { // change destiantion request
        glb_dst_t dst;

        if (glb_dst_parse (&dst, req, ctrl->default_port) < 0) {
            glb_log_info ("Ctrl: malformed change destination request: %s\n",
                          req);
            ctrl_respond (ctrl, fd, "Error\n");
            return 0;
        }

        int err;
        if (ctrl->wdog) {
            err = glb_wdog_change_dst   (ctrl->wdog,   &dst, true);
        }
        else {
            err = glb_router_change_dst (ctrl->router, &dst);
        }

        if (err < 0) {
#ifdef GLBD
            char tmp[128];
            glb_dst_print (tmp, 128, &dst);
            glb_log_info ("Ctrl: failed to apply destination change: %s", tmp);
#endif /* GLBD */
            ctrl_respond (ctrl, fd, "Error\n");
            return 0;
        }

        ctrl_respond (ctrl, fd, "Ok\n");

        if (ctrl->pool && dst.weight < 0.0 && ctrl->wdog) {
            // destination was removed from router, drop all connections to it
            // watchdog will do it itself
            glb_pool_drop_dst (ctrl->pool, &dst.addr);
        }

        return 0;
    }
}

// returns true if fd is ready to read
static inline bool
ctrl_fd_isset (glb_ctrl_t* ctrl, int idx)
{
    return (ctrl->fds[idx].revents & POLLIN);
}

static void*
ctrl_thread (void* arg)
{
    glb_ctrl_t* ctrl = arg;

#ifdef GLBD
    while (!glb_terminate) {
#else /* GLBD */
    while (true) {
#endif /* GLBD */
        long ret;

        // Timeout is needed to gracefully shut down
        ret = poll (ctrl->fds, ctrl->fd_max, 1000 /* ms */);
        if (ret < 0) {
            glb_log_error ("Error waiting for connections: %d (%s)",
                           errno, strerror(errno));
            goto err; //?
        }
        else if (0 == ret) continue;
        if (ctrl->inet_sock > 0 && (ctrl_fd_isset (ctrl, ctrl->inet_fd))) {
            // new network client
            int             client_sock;
            struct sockaddr client;
            socklen_t       client_size;

            client_sock = accept (ctrl->inet_sock, &client, &client_size);

            if (client_sock < 0) {
                glb_log_error ("Ctrl: failed to accept connection: %d (%s)",
                               errno, strerror (errno));
                goto err;
            }
            // Add to fds and wait for new events
            ctrl_add_client (ctrl, client_sock);
#ifdef GLBD
            if (ctrl->cnf->verbose) {
                glb_log_info ("Ctrl: accepted connection from %s\n",
                              glb_socket_addr_to_string (
                                  (glb_sockaddr_t*)&client, false));
            }
#endif
            continue;
        }
        else {
            int fd;
            for (fd = 0; fd <= ctrl->fd_max; fd++) { // find fd
                if (ctrl_fd_isset (ctrl, fd)) {
                    assert (fd != ctrl->inet_fd);
                    if (ctrl_handle_request (ctrl, ctrl->fds[fd].fd)) goto err;
                }
            }
            continue;
        }

    err:
        usleep (100000); // to avoid busy loop in case of error
    }

    return NULL;
}

glb_ctrl_t*
glb_ctrl_create (glb_cnf_t*    const cnf,
                 glb_router_t* const router,
                 glb_pool_t*   const pool,
                 glb_wdog_t*   const wdog,
                 uint16_t      const port,
                 int           const fifo,
                 int           const sock)
{
    if (fifo <= 0 && sock <= 0) return NULL;

    if (sock && listen (sock, CTRL_MAX)) {
        glb_log_error ("Ctrl: listen() failed: %d (%s)",
                       errno, strerror (errno));
        return NULL;
    }

    assert (NULL != router);

    glb_ctrl_t* ret = NULL;

    ret = calloc (1, sizeof (glb_ctrl_t));
    if (ret) {
        ret->cnf          = cnf;
        ret->router       = router;
        ret->pool         = pool;
        ret->wdog         = wdog;
        ret->fifo         = fifo;
        ret->inet_sock    = sock;
        ret->default_port = port;
        ret->fd_max       = 1; // at least one of fifo or inet_sock is present

        *(int*)&ret->inet_fd = -1;

        if (ret->fifo > 0) {
            ret->fds[0].fd      = ret->fifo;
            ret->fds[0].events  = POLLIN;

            if (ret->inet_sock > 0) {
                ret->fds[1].fd      = ret->inet_sock;
                ret->fds[1].events  = POLLIN;
                *(int*)&ret->inet_fd = 1;
                ret->fd_max  = 2;
            }
        }
        else if (ret->inet_sock > 0) {
            ret->fds[0].fd      = ret->inet_sock;
            ret->fds[0].events  = POLLIN;
            *(int*)&ret->inet_fd = 0;
        }

        if (pthread_create (&ret->thread, NULL, ctrl_thread, ret)) {
            glb_log_error ("Failed to launch ctrl thread.");
            free (ret);
            return NULL;
        }
        return ret;
    }
    else {
        glb_log_error ("Ctrl: out of memory.");
    }

    return NULL;
}

extern void
glb_ctrl_destroy (glb_ctrl_t* ctrl)
{
    pthread_join (ctrl->thread, NULL);
    free (ctrl);
}

