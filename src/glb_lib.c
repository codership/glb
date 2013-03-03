/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * Main GLB library unit.
 *
 * It's purpose is to overload the standard libc connect() call.
 *
 * $Id$
 */

#include "glb_log.h"
#include "glb_env.h"
#include "glb_router.h"
#include "glb_wdog.h"
#include "glb_control.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>

static int (*glb_real_connect) (int                    sockfd,
                                  const struct sockaddr* addr,
                                  socklen_t              addrlen) = NULL;

static glb_cnf_t*     glb_cnf         = NULL;

static glb_router_t*  glb_router      = NULL;

static void glb_init() __attribute__((constructor));

static void glb_init()
{
    glb_cnf = glb_env_parse();

    if (glb_cnf)
    {
        if (glb_cnf->verbose)
            glb_cnf_print(stdout, glb_cnf);

        glb_router = glb_router_create(glb_cnf);

        if (glb_router)
        {
            glb_wdog_t* wdog = NULL;

            if (glb_cnf->watchdog)
            {
                wdog = glb_wdog_create(glb_cnf, glb_router, NULL);
            }

            if (glb_cnf->ctrl_set)
            {
                uint16_t const default_port =
                    glb_sockaddr_get_port(&glb_cnf->inc_addr);

                int const sock = glb_socket_create(&glb_cnf->ctrl_addr, 0);

                if (sock > 0)
                    glb_ctrl_create(glb_cnf, glb_router, NULL, wdog,
                                    default_port, 0, sock);
            }
        }
    }

    if (!glb_router)
    {
        fputs ("Failed to initialize libglb.\n", stderr);
        fflush (stderr);
    }

    glb_real_connect = dlsym(RTLD_NEXT, "__connect");
}


static inline bool
glb_match_address(const struct sockaddr* const addr, socklen_t const addrlen)
{
    const struct sockaddr_in* addr_in = (const struct sockaddr_in*) addr;

    return (
        addr->sa_family   == AF_INET &&
        addr_in->sin_port == glb_cnf->inc_addr.sin_port &&
        !memcmp (&addr_in->sin_addr, &glb_cnf->inc_addr.sin_addr,
                 sizeof(struct in_addr))
        );
}


int connect(int const              sockfd,
            const struct sockaddr* addr,
            socklen_t const        addrlen)
{
    if (glb_router)
    {
        if (glb_match_address (addr, addrlen))
        {
            int ret = glb_router_connect(glb_router, sockfd);
            assert (ret == 0 || ret == -1);
            return ret;
        }
    }

    return glb_real_connect(sockfd, addr, addrlen);
}
