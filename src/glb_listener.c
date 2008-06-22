/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <pthread.h>
#include <assert.h>
#include <unistd.h>

#include "glb_listener.h"

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
    fd_set fds;

    // shoudl be no need to refresh the fd set
    FD_ZERO (&fds);
    FD_SET (listener->sock, &fds);

    while (1) {
        long           ret;
        int            client_sock;
        glb_sockaddr_t client;
        size_t         client_size;
        int            server_sock;
        glb_sockaddr_t server;

        ret = select (listener->sock + 1, &fds, NULL, NULL, NULL);
        if (ret < 0) {
            perror ("error waiting for connections");
            FD_SET (listener->sock, &fds);
            continue; //?
        }

        assert (1 == ret);
        assert (FD_ISSET (listener->sock, &fds));

        // first must connect to a destination!
        server_sock = glb_router_connect (listener->router, &server);
        if (server_sock < 0) {
            perror ("Listener: failed to connect to destination");
            usleep (1000000); // to avoid busy loop
            continue;
        }

        client_sock = accept (listener->sock,
                              (struct sockaddr*) &client, &client_size);
        if (client_sock < 0) {
            perror ("Listener: failed to accept connection");
            goto err1;
        }

        ret = glb_pool_add_conn (listener->pool, client_sock, server_sock,
                                 &server);
        if (ret < 0) {
            perror ("Listener: failed to add connection to pool");
            goto err2;
        }

        fprintf (stderr, "Listener: accepted connection from %s ",
                 glb_socket_addr_to_string (&client));
        fprintf (stderr, "to %s\n",
                 glb_socket_addr_to_string (&server));
        continue;

    err2:
        close (client_sock);
    err1:
        close (server_sock);
        glb_router_disconnect (listener->router, &server);
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
        perror ("Failed to create listening socket");
        return NULL;
    }

    if (listen (sock, 10)) { // what should be the queue length?
        perror ("listen() failed");
        return NULL;
    }

    ret = calloc (1, sizeof (glb_listener_t*));
    if (ret) {
        ret->sock   = sock;
        ret->router = router;
        ret->pool   = pool;

        if (pthread_create (&ret->thread, NULL, listener_thread, ret)) {
            perror ("Failed to launch listen thread");
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
    perror ("glb_listener_destroy() not implemented");
}

