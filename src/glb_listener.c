/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
typedef struct pollfd pollfd_t;

#include "glb_log.h"
#include "glb_listener.h"

extern bool glb_verbose;

struct glb_listener
{
    int           sock;
    glb_router_t* router;
    glb_pool_t*   pool;
    pthread_t     thread;
};

static void*
listener_thread (void* arg)
{
    glb_listener_t* listener = arg;
    pollfd_t pollfd = { .fd = listener->sock, .events = POLLIN, .revents = 0 };

    while (1) {
        long           ret;
        int            client_sock;
        glb_sockaddr_t client;
        socklen_t      client_size;
        int            server_sock;
        glb_sockaddr_t server;

        ret = poll (&pollfd, 1, -1);
        if (ret < 0) {
            glb_log_error ("Error waiting for connections: %d (%s)",
                           errno, strerror (errno));
            goto err; //?
        }

        assert (1 == ret);
        assert (pollfd.revents & POLLIN);

        client_sock = accept (listener->sock,
                              (struct sockaddr*) &client, &client_size);
        if (client_sock < 0) {
            glb_log_error ("Failed to accept connection: %d (%s)",
                           errno, strerror (errno));
            goto err;
        }

        server_sock = glb_router_connect (listener->router, &server);
        if (server_sock < 0) {
            glb_log_error("Failed to connect to destination: %d (%s)",
                          errno, strerror(errno));
            goto err1;
        }

        ret = glb_pool_add_conn (listener->pool, client_sock, server_sock,
                                 &server);
        if (ret < 0) {
            glb_log_error ("Failed to add connection to pool: "
                           "%d (%s)", errno, strerror (errno));
            goto err2;
        }

        if (glb_verbose) {
            glb_log_info ("Accepted connection from %s ",
                          glb_socket_addr_to_string (&client));
            glb_log_info ("to %s\n",
                          glb_socket_addr_to_string (&server));
        }
        continue;

    err2:
        close (server_sock);
        glb_router_disconnect (listener->router, &server);
    err1:
        close (client_sock);
    err:
        usleep (100000); // to avoid busy loop in case of error
    }

    return NULL;
}

glb_listener_t*
glb_listener_create (glb_sockaddr_t* addr,
                     glb_router_t*   router,
                     glb_pool_t*     pool)
{
    glb_listener_t* ret;
    int sock = glb_socket_create (addr);

    if (sock < 0) {
        glb_log_error ("Failed to create listening socket: %d (%s)",
                       errno, strerror (errno));
        return NULL;
    }

    if (listen (sock, 10)) { // what should be the queue length?
        glb_log_error ("listen() failed: %d (%s)", errno, strerror (errno));
        return NULL;
    }

    ret = calloc (1, sizeof (glb_listener_t));
    if (ret) {
        ret->sock   = sock;
        ret->router = router;
        ret->pool   = pool;

        if (pthread_create (&ret->thread, NULL, listener_thread, ret)) {
            glb_log_error ("Failed to launch listener thread: %d (%s)",
                           errno, strerror (errno));
            close (sock);
            free (ret);
            ret = NULL;
        }
    }
    return ret;
}

extern void
glb_listener_destroy (glb_listener_t* listener)
{
    glb_log_error ("glb_listener_destroy() not implemented");
}

