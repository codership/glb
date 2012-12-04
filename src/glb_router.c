/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_log.h"
#include "glb_cmd.h"
#include "glb_misc.h"
#include "glb_socket.h"
#include "glb_router.h"

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h> // for close()
#include <time.h>

typedef struct router_dst
{
    glb_dst_t dst;
    long      conns;  // how many connections use this destination
    double    usage;  // usage measure: weight/(conns + 1) - bigger wins
    time_t    failed; // last time connection to this destination failed
} router_dst_t;

struct glb_router
{
    glb_sockaddr_t  sock_out; // outgoing socket address
    pthread_mutex_t lock;
    long            busy_count;
    long            wait_count;
    pthread_cond_t  free;
    long            conns;
    unsigned int    seed; // seed for rng
    long            n_dst;
    router_dst_t*   dst;
};

static const  double router_div_prot = 1.0e-09; // protection against div by 0

static inline double
router_dst_usage (router_dst_t* d)
{ return (d->dst.weight / (d->conns + router_div_prot)); }

int
glb_router_change_dst (glb_router_t* router, const glb_dst_t* dst)
{
    long          i;
    void*         tmp;
    router_dst_t* d = NULL;

    GLB_MUTEX_LOCK (&router->lock);

    // try to find destination in the list
    for (i = 0; i < router->n_dst; i++) {
        if (glb_dst_is_equal(&((&router->dst[i])->dst), dst)) {
            d = &router->dst[i];
            break;
        }
    }

    // sanity check
    if (!d && dst->weight < 0) {
        GLB_MUTEX_UNLOCK (&router->lock);
        char tmp[256];
        glb_dst_print (tmp, sizeof(tmp), dst);
        glb_log_warn ("Command to remove inexisting destination: %s", tmp);
        return -EADDRNOTAVAIL;
    }

    if (!d || dst->weight < 0) {
        // cant remove/add destination while someone's connecting
        while (router->busy_count > 0) {
            router->wait_count++;
            pthread_cond_wait (&router->free, &router->lock);
            router->wait_count--;
            assert (router->wait_count >= 0);
        }
    }

    if (!d) { // add destination to the list

        assert (i == router->n_dst);

        tmp = realloc (router->dst, (router->n_dst + 1) * sizeof(router_dst_t));

        if (!tmp) {
            i = -ENOMEM;
        }
        else {
            router->dst = tmp;
            d = router->dst + router->n_dst;
            router->n_dst++;
            d->dst    = *dst;
            d->conns  = 0;
            d->usage  = router_dst_usage(d);
            d->failed = 0;
        }
    }
    else if (dst->weight < 0) { // remove destination from the list

        assert (d);
        assert (i >= 0 && i < router->n_dst);

        router->conns -= d->conns; assert (router->conns >= 0);

        if ((i + 1) < router->n_dst) {
            // it is not the last, move the rest to close the gap
            router_dst_t* next = d + 1;
            size_t len = (router->n_dst - i - 1)*sizeof(router_dst_t);
            memmove (d, next, len);
        }

        tmp = realloc (router->dst,
                       (router->n_dst - 1) * sizeof(router_dst_t));

        if (!tmp && (router->n_dst > 1)) {
            i = -ENOMEM;
        }
        else {
            router->dst = tmp;
            router->n_dst--;
        }
    }
    else if (d->dst.weight != dst->weight) {
        // update weight and usage
        d->dst.weight = dst->weight;
        d->usage      = router_dst_usage (d);
    }

    assert (router->n_dst >= 0);
    assert (0 == router->busy_count);

    if (router->wait_count > 0) pthread_cond_signal (&router->free);
    GLB_MUTEX_UNLOCK (&router->lock);
    return i;
}

static void
router_cleanup (glb_router_t* router)
{
    pthread_mutex_destroy (&router->lock);
    pthread_cond_destroy (&router->free);
    if (router->dst) free (router->dst);
    free (router);
}

glb_router_t*
glb_router_create (size_t n_dst, glb_dst_t const dst[])
{
    glb_router_t* ret = malloc (sizeof (glb_router_t));

    if (ret) {
        long i;

        pthread_mutex_init (&ret->lock, NULL);
        pthread_cond_init  (&ret->free, NULL);

        glb_socket_addr_init (&ret->sock_out, "0.0.0.0", 0); // client socket

        ret->busy_count = 0;
        ret->conns = 0;
        ret->seed  = getpid();
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
router_choose_dst_weight (glb_router_t* router)
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

// find a ready destination by client source hint
static router_dst_t*
router_choose_dst_hint (glb_router_t* router, uint32_t hint)
{
    if (router->n_dst <= 0) return NULL;

    /* First we attempt target predefined by hint (hint % router->n_dst).
     * If that fails, we iterate over the rest. But for every client
     * that falls onto that target we want to have a different iteration offset
     * (so that they don't all failover to the same destination), hence hint
     * is shifted, to get a fairly random (but deterministinc) offset.
     * That's why total number of attempts is N+1 */

    int      n = router->n_dst + 1;
    uint32_t i = hint;
    time_t now = time(NULL);

    hint  = hint >> 8;

    while (n > 0) {
        i %= router->n_dst;

        router_dst_t* d = &router->dst[i];

        if (difftime (now, d->failed) > DST_RETRY_INTERVAL) {
            return d;
        }

        i = hint + n;
        n -= 1;
    }

    return NULL;
}

static inline router_dst_t*
router_choose_dst (glb_router_t* router, uint32_t hint)
{
    if (GLB_POLICY_LEAST == glb_cnf->policy) {
        return router_choose_dst_weight (router);
    }
    else {
        return router_choose_dst_hint (router, hint);
    }
}

// connect to a best destination, possiblly failing over to a next best
static int
router_connect_dst (glb_router_t*   const router,
                    int             const sock,
                    uint32_t        const hint,
                    glb_sockaddr_t* const addr)
{
    router_dst_t* dst;
    int  error    = EHOSTDOWN;
    int  ret;
    bool redirect = false;

    GLB_MUTEX_LOCK (&router->lock);

    router->busy_count++;

    // keep trying until we run out of destinations
    while ((dst = router_choose_dst (router, hint))) {
        dst->conns++; router->conns++;
        dst->usage = router_dst_usage(dst);

        GLB_MUTEX_UNLOCK (&router->lock);

        ret = connect (sock, (struct sockaddr*)&dst->dst.addr,
                       sizeof (dst->dst.addr));

        GLB_MUTEX_LOCK (&router->lock);

        if (ret != 0) {
            error = errno;
            // connect failed, undo usage count, update destination failed mark
            dst->conns--; router->conns--;
            assert (dst->conns >= 0);
            dst->usage = router_dst_usage(dst);
            glb_log_warn ("Failed to connect to %s: %s",
                     glb_socket_addr_to_string (&dst->dst.addr),
                     strerror(error));
            dst->failed = time(NULL);
            redirect = true;
        }
        else {
            *addr = dst->dst.addr;
            if (redirect) {
                glb_log_warn ("Redirecting to %s",
                              glb_socket_addr_to_string (addr));
            }
            error = 0; // return success
            break;
        }
    }
    assert(dst != 0 || error != 0);

    router->busy_count--;
    assert (router->busy_count >= 0);

    if (0 == router->busy_count && 0 < router->wait_count)
        pthread_cond_signal (&router->free);

    GLB_MUTEX_UNLOCK (&router->lock);

    return -error; // all attempts failed, return last errno
}

// returns socket number or negative error code
int
glb_router_connect (glb_router_t* router, const glb_sockaddr_t* src_addr,
                    glb_sockaddr_t* dst_addr)
{
    int sock, ret;

    /* Here it is assumed that this function is called only from one thread. */
    if (router->conns >= glb_cnf->max_conn) {
        glb_log_warn ("Maximum connection limit of %ld exceeded. Rejecting "
                      "connection attempt.", glb_cnf->max_conn);
        return -EMFILE;
    }

    // prepare a socket
    sock = glb_socket_create (&router->sock_out, GLB_SOCK_NODELAY);
    if (sock < 0) {
        glb_log_error ("glb_socket_create() failed");
        return sock;
    }

    uint32_t hint = 0;
    switch (glb_cnf->policy)
    {
    case GLB_POLICY_LEAST:  break;
    case GLB_POLICY_RANDOM: hint = rand_r(&router->seed); break;
    case GLB_POLICY_SOURCE: hint = glb_socket_addr_hash(src_addr); break;
    case GLB_POLICY_MAX:    assert(0);
    };

    // attmept to connect until we run out of destinations
    ret = router_connect_dst (router, sock, hint, dst_addr);

    // avoid socket leak
    if (ret < 0) {
        glb_log_error ("router_connect_dst() failed.");
        close (sock);
        sock = ret;
    }

    return sock;
}

void
glb_router_disconnect (glb_router_t* router, const glb_sockaddr_t* dst)
{
    long i;

    GLB_MUTEX_LOCK (&router->lock);

    for (i = 0; i < router->n_dst; i++) {
        router_dst_t* d = &router->dst[i];
        if (glb_socket_addr_is_equal (&d->dst.addr, dst)) {
            d->conns--; router->conns--;
            assert(d->conns >= 0);
            d->usage = router_dst_usage(d);
            break;
        }
    }

    if (i == router->n_dst) {
        glb_log_warn ("Attempt to disconnect from non-existing destination: %s",
                      glb_socket_addr_to_string(dst));
    }

    GLB_MUTEX_UNLOCK (&router->lock);
}

size_t
glb_router_print_info (glb_router_t* router, char* buf, size_t buf_len)
{
    size_t len = 0;
    long   total_conns;
    long   n_dst;
    long   i;

    len += snprintf (buf + len, buf_len - len, "Router:\n"
                     "----------------------------------------------------\n"
                     "        Address       :   weight   usage   conns\n");
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

    GLB_MUTEX_LOCK (&router->lock);

    for (i = 0; i < router->n_dst; i++) {
        router_dst_t* d = &router->dst[i];

        len += snprintf (buf + len, buf_len - len, "%s : %8.3f %7.3f %5ld\n",
                         glb_socket_addr_to_string(&d->dst.addr),
                         d->dst.weight, 1.0/(d->usage + 1.0),
                         d->conns);
        if (len == buf_len) {
            buf[len - 1] = '\0';
            return (len - 1);
        }
    }

    n_dst = router->n_dst;
    total_conns = router->conns;

    GLB_MUTEX_UNLOCK (&router->lock);

    len += snprintf (buf + len, buf_len - len,
                     "----------------------------------------------------\n"
                     "Destinations: %ld, total connections: %ld of %ld max\n",
                     n_dst, total_conns, glb_cnf->max_conn);
    if (len == buf_len) {
        buf[len - 1] = '\0';
        return (len - 1);
    }

     return len;
}
