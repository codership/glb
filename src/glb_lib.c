/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * Main GLB library unit.
 *
 * It's purpose is to overload the standard libc connect() call.
 *
 * $Id$
 */

#define _GNU_SOURCE 1

#include "glb_env.h"
#include "glb_router.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

static bool __glb_initialized     = false;

static glb_cnf_t*    __glb_cnf    = NULL;

static glb_router_t* __glb_router = NULL;

static int (*__glb_real_connect) (int                    sockfd,
                                  const struct sockaddr* addr,
                                  socklen_t              addrlen) = NULL;

static void
__glb_init()
{
    __glb_cnf = glb_env_parse();

    if (__glb_cnf)
    {
        __glb_router = glb_router_create(__glb_cnf);
    }

    __glb_real_connect = dlsym(RTLD_NEXT, "__connect");

    __glb_initialized = true;
}

static inline bool
__glb_match_address(const struct sockaddr* const addr, socklen_t const addrlen)
{
    const struct sockaddr_in* addr_in = (const struct sockaddr_in*) addr;

    return (
        addr->sa_family   == AF_INET &&
        addr_in->sin_port == __glb_cnf->inc_addr.sin_port &&
        !memcmp (&addr_in->sin_addr, &__glb_cnf->inc_addr.sin_addr,
                 sizeof(struct in_addr))
        );
}

int connect(int                    const sockfd,
            const struct sockaddr* const addr,
            socklen_t              const addrlen)
{
    if (!__glb_initialized) __glb_init();

    if (__glb_router)
    {
        if (__glb_match_address (addr, addrlen))
        {
            puts("DEBUG: Connecting by router");
            return __glb_router_connect(__glb_router, sockfd);
        }
    }
    puts ("DEBUG: Connecting directly");

    return __glb_real_connect(sockfd, addr, addrlen);
}
