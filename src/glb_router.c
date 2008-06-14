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

#include "glb_router.h"

typedef struct router_dst
{
    glb_dst_t dst;
    double    weight;
    long      conns; // how many connections use this destination
    double    usage; // usage measure: weight/(conns + 1) - bigger wins
    bool      ready; // if destinaiton accepts connecitons
} router_dst_t;

struct glb_router
{
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

        if (glb_dst_equal(&d->dst, dst)) {
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

    // not found in the list, add destination
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

out:
    pthread_mutex_unlock (&router->lock);
    return i;
}

glb_router_t*
glb_router_create (size_t n_dst, glb_dst_t dst[])
{
    glb_router_t* ret = malloc (sizeof (glb_router_t));

    if (ret) {
        size_t i;

        pthread_mutex_init (&ret->lock, NULL);
        ret->n_dst = 0;
        ret->dst   = NULL;

        for (i = 0; i < n_dst; i++) {
            if (glb_router_change_dst(ret, &dst[i]) < 0) {
                if (ret->dst) free (ret->dst);
                free (ret);
                return NULL;
            }
        }

        assert (ret->n_dst = n_dst);
    }

    return ret;
}

#if 0
int
glb_router_connect (glb_router_t* router)
{
    int ret;
    int i;
    double usage = router->dst[0].usage;
    
}
#endif
