/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_listener.h"
#include "glb_log.h"
#include "glb_limits.h"
#include "glb_cmd.h"

#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>

typedef struct pollfd pollfd_t;

struct glb_listener
{
    const glb_cnf_t* cnf;
    glb_router_t* router;
    glb_pool_t*   pool;
    pthread_t     thread;
    int           sock;
};

static void*
listener_thread (void* arg)
{
    glb_listener_t* listener = arg;

    while (1) {
        int            ret;
        int            client_sock;
        glb_sockaddr_t client;
        socklen_t      client_size;
        int            server_sock;
        glb_sockaddr_t server;

        client_sock = accept (listener->sock,
                              (struct sockaddr*) &client, &client_size);
        if (client_sock < 0) {
            glb_log_error ("Failed to accept connection: %d (%s)",
                           errno, strerror (errno));
            goto err;
        }

        ret = glb_router_connect(listener->router, &client ,&server,
                                 &server_sock);
        if (server_sock < 0) {
            if (server_sock != -EMFILE)
                glb_log_error("Failed to connect to destination: %d (%s)",
                              -server_sock, strerror(-server_sock));
            goto err1;
        }

        assert (0 == ret || -EINPROGRESS == ret);

        glb_socket_setopt(client_sock, GLB_SOCK_NODELAY); // ignore error here

        ret = glb_pool_add_conn (listener->pool, client_sock, server_sock,
                                 &server, 0 == ret);
        if (ret < 0) {
            glb_log_error ("Failed to add connection to pool: "
                           "%d (%s)", -ret, strerror (-ret));
            goto err2;
        }

        if (listener->cnf->verbose) {
            glb_log_info ("Accepted connection from %s ",
                          glb_socket_addr_to_string (&client));
            glb_log_info ("to %s\n",
                          glb_socket_addr_to_string (&server));
        }
        continue;

    err2:
        close (server_sock);
        glb_router_disconnect (listener->router, &server, false);
    err1:
        close (client_sock);
    err:
        usleep (100000); // to avoid busy loop in case of error
    }

    return NULL;
}

glb_listener_t*
glb_listener_create (const glb_cnf_t* const cnf,
                     glb_router_t*    const router,
                     glb_pool_t*      const pool,
                     int              const sock)
{
    glb_listener_t* ret = NULL;

    if (listen (sock,
                cnf->max_conn ? cnf->max_conn : (1U << 14)/* 16K */)){
        glb_log_error ("listen() failed: %d (%s)", errno, strerror (errno));
        return NULL;
    }

    ret = calloc (1, sizeof (glb_listener_t));
    if (ret) {
        ret->cnf    = cnf;
        ret->router = router;
        ret->pool   = pool;
        ret->sock   = sock;

        if (pthread_create (&ret->thread, NULL, listener_thread, ret)) {
            glb_log_error ("Failed to launch listener thread: %d (%s)",
                           errno, strerror (errno));
            free (ret);
            ret = NULL;
        }
    }
    else
    {
        glb_log_error ("Failed to allocate listener object: %d (%s)",
                       errno, strerror (errno));
    }

    return ret;
}

extern void
glb_listener_destroy (glb_listener_t* listener)
{
    glb_log_error ("glb_listener_destroy() not implemented");
}

