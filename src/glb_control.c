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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include "glb_log.h"
#include "glb_signal.h"
#include "glb_control.h"

extern bool glb_verbose;

static const char ctrl_getinfo_cmd[] = "getinfo";
static const char ctrl_getstat_cmd[] = "getstat";

struct glb_ctrl
{
    pthread_t     thread;
    const char*   fifo_name;
    int           fifo;
    int           inet_sock;
    glb_router_t* router;
    glb_pool_t*   pool;
    int           fd_max;
    fd_set        fds;
    uint16_t      default_port;
};

static void
ctrl_add_client (glb_ctrl_t* ctrl, int fd)
{
    assert (ctrl->fd_max != fd);
    FD_SET (fd, &ctrl->fds);
    if (fd > ctrl->fd_max) ctrl->fd_max = fd;
}

static void
ctrl_del_client (glb_ctrl_t* ctrl, int fd)
{
    assert (ctrl->fd_max >= fd);
    FD_CLR (fd, &ctrl->fds);
    if (fd == ctrl->fd_max) { // find next highest fd
        while (!FD_ISSET (ctrl->fd_max, &ctrl->fds)) ctrl->fd_max--;
    }
    assert (ctrl->fd_max < fd);
    close (fd);
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
        ctrl_respond (ctrl, fd, "OK\n");

        if (dst.weight < 0.0) {
            // destination was removed from router, drop all connections to it
            glb_pool_drop_dst (ctrl->pool, &dst.addr);
        }
        return 0;
    }
}

static void*
ctrl_thread (void* arg)
{
    glb_ctrl_t* ctrl = arg;

    while (!glb_terminate) {
        long            ret;
        int             client_sock;
        struct sockaddr client;
        socklen_t       client_size;
        fd_set          fds = ctrl->fds;
        struct timeval  timeout = { 1, 0 };

        ret = select (ctrl->fd_max + 1, &fds, NULL, NULL, &timeout);
        if (ret < 0) {
            perror ("error waiting for connections");
            goto err; //?
        }
        else if (0 == ret) continue;

        if (ctrl->inet_sock > 0 && FD_ISSET (ctrl->inet_sock, &fds)) {
            client_sock = accept (ctrl->inet_sock, &client, &client_size);
            if (client_sock < 0) {
                glb_log_error ("Ctrl: failed to accept connection: %d (%s)",
                               errno, strerror (errno));
                goto err;
            }

            // Add to fds and wait for new events
            ctrl_add_client (ctrl, client_sock);

            if (glb_verbose) {
                fprintf (stderr, "Ctrl: accepted connection from %s\n",
                         glb_socket_addr_to_string ((glb_sockaddr_t*)&client));
            }
            continue;
        }
        else {
            int fd;
            for (fd = 1; fd <= ctrl->fd_max; fd++) { // find fd
                if (FD_ISSET (fd, &fds)) break;
            }
            if (ctrl_handle_request (ctrl, fd)) goto err;
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
        ret->fd_max       = fifo > inet_sock ? fifo : inet_sock;
        ret->default_port = port;

        FD_ZERO (&ret->fds);
        FD_SET  (ret->fifo, &ret->fds);
        if (ret->inet_sock > 0) FD_SET (ret->inet_sock, &ret->fds);

#if 0 // remove
        for (fd = 0; fd < FD_SETSIZE; fd++) {
            if (FD_ISSET (fd, &ret->fds))
                printf ("Ctrl: set fd %d\n", fd);
        }
        printf ("Ctrl: fd_max = %d\n", ret->fd_max);
        printf ("Ctrl: ctrl object: %p\n", ret);
#endif
        if (pthread_create (&ret->thread, NULL, ctrl_thread, ret)) {
            glb_log_error ("Failed to launch ctrl thread.");
            free (ret);
            goto err2;
        }
        return ret;
    }
    else {
        perror ("Ctrl: malloc()");
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

