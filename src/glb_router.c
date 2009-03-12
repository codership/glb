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
#include <unistd.h> // for close()
#include <time.h>

#include "glb_log.h"
#include "glb_socket.h"
#include "glb_router.h"

extern bool glb_verbose;

typedef struct router_dst
{
    glb_dst_t dst;
    double    weight;
    long      conns;  // how many connections use this destination
    double    usage;  // usage measure: weight/(conns + 1) - bigger wins
    time_t    failed; // last time connection to this destination failed
} router_dst_t;

struct glb_router
{
    glb_sockaddr_t  sock_out; // outgoing socket address
    pthread_mutex_t lock;
    long            n_dst;
    router_dst_t*   dst;
};

long
glb_router_change_dst (glb_router_t* router, const glb_dst_t* dst)
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
                if (!tmp && (router->n_dst > 1)) {
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
        char tmp[128];
        glb_dst_print (tmp, 128, dst);
        glb_log_warn ("Command to remove inexisting destination: %s", tmp);
        i = -EADDRNOTAVAIL;
        goto out;
    }

    tmp = realloc (router->dst, (router->n_dst + 1) * sizeof(router_dst_t));
    if (!tmp) {
        i = -ENOMEM;
        goto out;
    }

    router->dst = tmp;
    d = router->dst + router->n_dst;
    router->n_dst++;
    d->dst    = *dst;
    d->weight = dst->weight;
    d->conns  = 0;
    d->usage  = d->weight / (d->conns + 1);
    d->failed = 0;

out:
    assert (router->n_dst >= 0);
    pthread_mutex_unlock (&router->lock);
    return i;
}

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
        long i;

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

        assert (ret->n_dst == n_dst);
    }

    return ret;
}

void
glb_router_destroy (glb_router_t* router)
{
    router_cleanup (router);
}

// seconds (should be > 1 due to time_t precision)
static const double DST_RETRY_INTERVAL = 2.0;

// find a ready destination with minimal usage
static router_dst_t*
router_choose_dst (glb_router_t* router)
{
    router_dst_t* ret = NULL;

    if (router->n_dst > 0) {
        double max_usage = 0.0;
        int    i;
        time_t now = time(NULL);

        for (i = 0; i < router->n_dst; i++) {
            router_dst_t* d = &router->dst[i];

            if (d->usage > max_usage &&
                difftime (now, d->failed) > DST_RETRY_INTERVAL) {
                ret = d;
                max_usage = d->usage;
            }
        }
    }

    return ret;
}

// connect to a best destination, possiblly failing over to a next best
static int
router_connect_dst (glb_router_t* router, int sock, glb_sockaddr_t* addr)
{
    router_dst_t* dst;
    int error = 1;

    // keep trying until we run out of destinations
    while ((dst = router_choose_dst (router))) {
        if (connect (sock, (struct sockaddr*)&dst->dst.addr,
                     sizeof (dst->dst.addr))) {
	    error = errno;
            // connect failed, update destination failed mark
            fprintf (stderr, "Router: failed to connect to %s: %s\n",
                     glb_socket_addr_to_string (&dst->dst.addr), strerror(error));
            dst->failed = time(NULL);
        }
        else {
            // success, update stats
            dst->conns++;
            dst->usage = dst->weight / (dst->conns + 1);
            *addr = dst->dst.addr;
            return 0;
        }
    }

    return -error; // all attempts failed, return last errno
}

// returns error code
int
glb_router_connect (glb_router_t* router, glb_sockaddr_t* dst_addr)
{
    int sock, ret;

    // prepare a socket
    sock = glb_socket_create (&router->sock_out);
    if (sock < 0) {
        perror ("Router: glb_socket_create");
        return sock;
    }

    if (pthread_mutex_lock (&router->lock)) {
        perror ("Router mutex lock failed, abort.");
        abort();
    }

    // attmept to connect until we run out of destinations
    ret = router_connect_dst (router, sock, dst_addr);

    // avoid socket leak
    if (ret < 0) {
        perror ("Router: router_connect_dst()");
        close (sock);
        sock = ret;
    }

    pthread_mutex_unlock (&router->lock);

    return sock;
}

void
glb_router_disconnect (glb_router_t* router, const glb_sockaddr_t* dst)
{
    long i;

    if (pthread_mutex_lock (&router->lock)) {
        perror ("Router mutex lock failed, abort.");
        abort();
    }

    for (i = 0; i < router->n_dst; i++) {
        router_dst_t* d = &router->dst[i];
        if (glb_socket_addr_is_equal (&d->dst.addr, dst)) {
            d->conns--;
            d->usage = d->weight / (d->conns + 1);
            break;
        }
    }

    if (i == router->n_dst) {
        perror ("Attempt to disconnect from non-existing destination");
        perror (glb_socket_addr_to_string(dst));
    }

    pthread_mutex_unlock (&router->lock);
}

size_t
glb_router_print_stats (glb_router_t* router, char* buf, size_t buf_len)
{
    size_t len = 0;
    long   total_conns = 0;
    long   n_dst;
    long   i;

    len += snprintf (buf + len, buf_len - len, "Router:\n"
                     "----------------------------------------------------\n"
                     "        Address       :   weight   usage   conns\n");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    if (pthread_mutex_lock (&router->lock)) {
        perror ("Router mutex lock failed, abort.");
        abort();
    }

    for (i = 0; i < router->n_dst; i++) {
        router_dst_t* d = &router->dst[i];

        total_conns += d->conns;

        len += snprintf (buf + len, buf_len - len, "%s : %6ld %9.3f %6ld\n",
                         glb_socket_addr_to_string(&d->dst.addr),
                         d->dst.weight, 1.0 - (2*d->usage / (d->weight + 1)),
			 d->conns);
        if (len == buf_len) {
            buf[len - 1] = '\0';
            return (len - 1);
        }
    }

    n_dst = router->n_dst;

    pthread_mutex_unlock (&router->lock);

    len += snprintf (buf + len, buf_len - len,
                     "----------------------------------------------------\n"
                     "Destinations: %ld, total connections: %ld\n",
                     n_dst, total_conns);
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

     return len;
}
