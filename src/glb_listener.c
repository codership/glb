/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_listener.h"

struct glb_listener
{
    int listen_sock;
};

glb_listener_t*
glb_listener_create (glb_sockaddr_t* addr,
                     glb_router_t*   router,
                     glb_pool_t*     pool)
{
    glb_listener_t* ret = NULL;
    return ret;
}

extern void
glb_listener_destroy (glb_listener_t* listener)
{
    perror ("glb_listener_destroy() not implemented");
}

