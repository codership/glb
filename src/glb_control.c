/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

// keep asserts here for now
#undef NDEBUG

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

// unfotunately I see no way to use glb_pool.c polling code in here
// so it is yet another implementation
#include <poll.h>
typedef struct pollfd pollfd_t;

#include "glb_log.h"
#include "glb_signal.h"
#include "glb_control.h"

extern bool glb_verbose;

static const char ctrl_getinfo_cmd[] = "getinfo";
static const char ctrl_getstat_cmd[] = "getstat";

typedef enum ctrl_fd
{
    CTRL_FIFO = 0,
    CTRL_LISTEN,
    CTRL_MAX = 32 // max 30 simultaneous control connections
} ctrl_fd_t;

struct glb_ctrl
{
    pthread_t     thread;
    const char*   fifo_name;
    int           fifo;
    int           inet_sock;
    glb_router_t* router;
    glb_pool_t*   pool;
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

    if (CTRL_MAX == ctrl->fd_max) // no more clients
        ctrl->fds[CTRL_LISTEN].events = 0;
}

static void
ctrl_del_client (glb_ctrl_t* ctrl, int fd)
{
    int idx;

    assert (CTRL_MAX >= ctrl->fd_max);
    assert (ctrl->fd_max > CTRL_LISTEN);

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

    ctrl->fds[CTRL_LISTEN].events = POLLIN;
}

static inline void
ctrl_respond (glb_ctrl_t* ctrl, int fd, const char* resp)
{
    if (fd != ctrl->fifo) {
        // can't respond to FIFO, as will immediately read it back
        write (fd, resp, strlen(resp));
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

    if (ret < 0) return -1;

    if (0 == req_size) { // connection closed
        ctrl_del_client (ctrl, fd);
        return 0;
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
    else if (!strncasecmp (ctrl_getstat_cmd, req, strlen(ctrl_getstat_cmd))) {
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

        if (glb_router_change_dst (ctrl->router, &dst) < 0) {
            char tmp[128];
            glb_dst_print (tmp, 128, &dst);
            glb_log_info ("Ctrl: failed to apply destination change: %s", tmp);
            ctrl_respond (ctrl, fd, "Error\n");
            return 0;
        }

        ctrl_respond (ctrl, fd, "Ok\n");

        if (dst.weight < 0.0) {
            // destination was removed from router, drop all connections to it
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

    while (!glb_terminate) {
        long ret;

        // Timeout is needed to gracefully shut down
        ret = poll (ctrl->fds, ctrl->fd_max, 1000 /* ms */);
        if (ret < 0) {
            glb_log_error ("Error waiting for connections: %d (%s)",
                           errno, strerror(errno));
            goto err; //?
        }
        else if (0 == ret) continue;

        if (ctrl->inet_sock > 0 && (ctrl_fd_isset (ctrl, CTRL_LISTEN))) {
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

            if (glb_verbose) {
                glb_log_info ("Ctrl: accepted connection from %s\n",
                         glb_socket_addr_to_string ((glb_sockaddr_t*)&client));
            }
            continue;
        }
        else {
            int fd;
            for (fd = CTRL_FIFO; fd <= ctrl->fd_max; fd++) { // find fd
                if (ctrl_fd_isset (ctrl, fd)) {
                    assert (fd != CTRL_LISTEN);
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
glb_ctrl_create (glb_router_t*         router,
                 glb_pool_t*           pool,
                 uint16_t              port,
                 const char*           name,
                 const glb_sockaddr_t* inet_addr)
{
    glb_ctrl_t* ret = NULL;
    int inet_sock = 0;
    int fifo;
    const char* fifo_name;

    assert (NULL != router);
    assert (NULL != name);

    fifo_name = strdup (name); // for future cleanup
    if (!fifo_name) {
        glb_log_error ("Ctrl: strdup(): %d (%s)", errno, strerror (errno));
        return NULL;
    }

    if (mkfifo (fifo_name, S_IRUSR | S_IWUSR)) {
        glb_log_error ("FIFO '%s' already exists. Check that no other "
                       "glbd instance is running and delete it "
                       "or specify another name with --fifo option.",
                       fifo_name);
        goto err;
    }

    fifo = open (fifo_name, O_RDWR);
    if (fifo < 0) {
        glb_log_error ("Ctrl: failed to open FIFO file: %d (%s)",
                       errno, strerror (errno));
        goto err;
    }

    if (inet_addr) {
        inet_sock = glb_socket_create (inet_addr);
        if (inet_sock < 0) {
            glb_log_error ("Ctrl: failed to create listening socket: %d (%s)",
                           errno, strerror (errno));
            goto err1;
        }

        if (listen (inet_sock, 10)) { // what should be the queue length?
            glb_log_error ("Ctrl: listen() failed: %d (%s)",
                           errno, strerror (errno));
            goto err2;
        }
    }

    ret = calloc (1, sizeof (glb_ctrl_t));
    if (ret) {
        ret->router       = router;
        ret->pool         = pool;
        ret->fifo_name    = fifo_name;
        ret->fifo         = fifo;
        ret->inet_sock    = inet_sock;
        ret->fd_max       = fifo > inet_sock ? 1 : 2;
        ret->default_port = port;

        ret->fds[CTRL_FIFO].fd      = ret->fifo;
        ret->fds[CTRL_FIFO].events  = POLLIN;

        if (ret->inet_sock > 0) {
            ret->fds[CTRL_LISTEN].fd      = ret->inet_sock;
            ret->fds[CTRL_LISTEN].events  = POLLIN;
        }

        if (pthread_create (&ret->thread, NULL, ctrl_thread, ret)) {
            glb_log_error ("Failed to launch ctrl thread.");
            free (ret);
            goto err2;
        }
        return ret;
    }
    else {
        glb_log_error ("Ctrl: out of memory.");
    }

err2:
    close (inet_sock);
err1:
    close (fifo);
    remove (fifo_name);
err:
    free ((char*)fifo_name);
    return NULL;
}

extern void
glb_ctrl_destroy (glb_ctrl_t* ctrl)
{
    pthread_join (ctrl->thread, NULL);
    if (ctrl->fifo) close (ctrl->fifo);
    if (ctrl->inet_sock) close (ctrl->inet_sock);
    remove (ctrl->fifo_name);
    free ((char*)ctrl->fifo_name);
    free (ctrl);
}

