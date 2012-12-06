/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * Main GLB library unit
 *
 * $Id$
 */

#define _GNU_SOURCE 1

#include "glb_cnf.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

int (*__glb_real_connect) (int sockfd,
                           const struct sockaddr *addr,
                           socklen_t addrlen) = NULL;

const glb_cnf_t* glb_cnf = NULL;

static glb_router_t* router = NULL;


int connect(int sockfd,
            const struct sockaddr *addr,
            socklen_t addrlen)
{

if (!__glb_real_connect)
__glb_real_connect = dlsym(RTLD_NEXT, "__connect");

if (!router)
{
}

if (address_match_vip)
{
}
else
{
return __glb_real_connect(sockfd, addr, addrlen);
}
}
