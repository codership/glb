/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "glb_socket.h"

int
glb_socket_in_addr (struct in_addr* addr, const char* hostname)
{
    struct hostent *host;
    host = gethostbyname (hostname);
    if (host == NULL)
    {
        fprintf (stderr, "Unknown host %s.\n", hostname);
        return -EINVAL;
    }
    *addr = *(struct in_addr *) host->h_addr;
    return 0;
}

void
glb_socket_sockaddr_in (struct sockaddr_in* addr,
                        struct in_addr*     host,
                        uint16_t            port)
{
    addr->sin_family = AF_INET;
    addr->sin_port   = htons (port);
    addr->sin_addr   = *host;
}

int
glb_socket_create (struct sockaddr_in* addr)
{
    int sock;

    /* Create the socket. */
    sock = socket (PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror ("socket");
        return -errno;
    }

    /* Give the socket a name. */
    if (bind (sock, (struct sockaddr *) addr, sizeof (*addr)) < 0)
    {
        perror ("bind");
        return -errno;
    }

    return sock;
}

