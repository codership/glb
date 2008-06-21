/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "glb_socket.h"
#include "glb_router.h"

typedef struct router_dst
{
    glb_dst_t dst;
    double    weight;
    long      conns; // how many connections use this destination
    double    usage; // usage measure: weight/(conns + 1) - bigger wins
    bool      ready; // if destinaiton accepts connecitons

//    glb_sockaddr_t addr; // destinaiton address to connect
} router_dst_t;

struct glb_router
{
    glb_sockaddr_t  sock_out; // outgoing socket address
    pthread_mutex_t lock;
    size_t          n_dst;
    router_dst_t*   dst;
};

long
glb_router_change_dst (glb_router_t* router, glb_dst_t* dst)
{
    long          i;
    void*         tmp;
    router_dst_t* d;

    if (pthread_mutex_lock (&router->lock)) {
        fprintf (stderr, "Router mutex lock failed, abort.");
        abort();
    }

    // try to find destination in the list
    for (i = 0; i < router->n_dst; i++) {
        d = &router->dst[i];

        if (glb_dst_is_equal(&d->dst, dst)) {
            if (dst->weight < 0) {
                // remove destination from the list
                if ((i + 1) < router->n_dst) {
                    // it is not the last, move the rest to close the gap
                    router_dst_t* next = d + 1;
                    size_t len = (router->n_dst - i - 1)*sizeof(router_dst_t);
                    memmove (d, next, len);
                }
                tmp = realloc (router->dst,
                               (router->n_dst - 1) * sizeof(router_dst_t));
                if (!tmp) {
                    i = -ENOTRECOVERABLE;
                    goto out;
                }

                router->dst = tmp;
                router->n_dst--;
            }
            else if (d->dst.weight != dst->weight) {
                // update weight and usage
                d->dst.weight = dst->weight;
                d->weight     = dst->weight;
                d->usage      = d->weight / (d->conns + 1);
            }
            goto out;
        }
    }
    assert (i == router->n_dst);

    // not found in the list, add destination, but first check weight
    if (dst->weight < 0) {
        fprintf (stderr, "WARNING: Command to remove inexisting destination: ");
        glb_dst_print (stderr, dst);
    }

    tmp = realloc (router->dst, (router->n_dst + 1) * sizeof(router_dst_t));
    if (!tmp) {
        i = -ENOMEM;
        goto out;
    }

    router->dst = tmp;
    router->n_dst++;
    d = &router->dst[i];
    d->dst    = *dst;
    d->weight = dst->weight;
    d->conns  = 0;
    d->usage  = d->weight / (d->conns + 1);
    d->ready  = true;

    // initialize sockaddr (to establish connections)
//    glb_socket_addr_init (&d->addr, &dst->addr, dst->port);

out:
    assert (router->n_dst >= 0);
    pthread_mutex_unlock (&router->lock);
    return i;
}

#if 0
// creates a client socket address for outgoing connections
static void
router_client_sockaddr_in (struct sockaddr_in* addr)
{
    struct in_addr host;

    if (glb_socket_in_addr (&host, "0.0.0.0")) {
        perror ("glb_socket_in_addr");
        abort();
    }
    glb_socket_sockaddr_in (addr, &host, 0);
}
#endif

static void
router_cleanup (glb_router_t* router)
{
    pthread_mutex_destroy (&router->lock);
    if (router->dst) free (router->dst);
    free (router);
}

glb_router_t*
glb_router_create (size_t n_dst, glb_dst_t dst[])
{
    glb_router_t* ret = malloc (sizeof (glb_router_t));

    if (ret) {
        size_t i;

        pthread_mutex_init (&ret->lock, NULL);
        glb_socket_addr_init (&ret->sock_out, "0.0.0.0", 0); // client socket
        ret->n_dst = 0;
        ret->dst   = NULL;

        for (i = 0; i < n_dst; i++) {
            if (glb_router_change_dst(ret, &dst[i]) < 0) {
                router_cleanup (ret);
                return NULL;
            }
        }

        assert (ret->n_dst = n_dst);
    }

    return ret;
}

// find a ready destination with minimal usage
static int
router_choose_dst (glb_router_t* router)
{
    int ret = -1;

    if (router->n_dst > 0) {
        double max_usage = 0.0;
        int    i;

        for (i = 0; i < router->n_dst; i++) {
            router_dst_t* d = &router->dst[i];
            if (d->ready && d->usage > max_usage) {
                ret = i;
                max_usage = d->usage;
            }
        }
    }

    return ret;
}

// connect to a best destination, possiblly failing over to a next best
static int
router_connect_dst (int sock, glb_router_t* router)
{
    int i;
    int ret;
    // keep trying until we run out of destinations
    while ((i = router_choose_dst (router)) >= 0) {
        router_dst_t* d = &router->dst[i];
        ret = connect (sock,
                       (struct sockaddr*)&d->dst.addr,
                       sizeof (d->dst.addr));
        if (ret) {
            // connect failed, mark destination bad
            d->ready = false;
            return -1;
        }
        else {
            // success, update stats
            d->conns++;
            d->usage = d->weight / (d->conns + 1);
            return sock;
        }
    }

    return -1;
}

// reset ready flag on destinations
// (TODO: associate a timestamp with a flag, don't reset right away)
static void
router_reset_dst (glb_router_t* router)
{
    size_t i;
    for (i = 0; i < router->n_dst; i++) {
        router->dst[i].ready = true;
    }
}

// returns connected socket
int
glb_router_connect (glb_router_t* router)
{
    int ret = -1;
    int sock;

    // prepare a socket
    sock = glb_socket_create (&router->sock_out);
    if (sock < 0) goto out;

    if (pthread_mutex_lock (&router->lock)) {
        fprintf (stderr, "Router mutex lock failed, abort.");
        abort();
    }

    // attmept to connect until we run out of destinations
    ret = router_connect_dst (sock, router);
    router_reset_dst(router);

    // avoid socket leak
    if (ret < 0) shutdown (sock, 2);
out:
    pthread_mutex_unlock (&router->lock);
    return ret;
}

