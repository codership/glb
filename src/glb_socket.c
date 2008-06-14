/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#include "glb_socket.h"

int
glb_socket_in_addr (struct in_addr* addr, const char* hostname)
{
    struct hostent *hostinfo;
    hostinfo = gethostbyname (hostname);
    if (hostinfo == NULL)
    {
        fprintf (stderr, "Unknown host %s.\n", hostname);
        return -EINVAL;
    }
    *addr = *(struct in_addr *) hostinfo->h_addr;
    return 0;
}

void
glb_socket_sockaddr (struct sockaddr_in* name,
                     struct in_addr*     host,
                     uint16_t            port)
{
    name->sin_family = AF_INET;
    name->sin_port   = htons (port);
    name->sin_addr   = *host;
}

int
glb_socket_make (struct in_addr* addr, uint16_t port)
{
    int sock;
    struct sockaddr_in name;

    /* Create the socket. */
    sock = socket (PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror ("socket");
        return -errno;
    }

    /* Give the socket a name. */
    glb_socket_sockaddr (&name, addr, port);
    if (bind (sock, (struct sockaddr *) &name, sizeof (name)) < 0)
    {
        perror ("bind");
        return -errno;
    }

    return sock;
}

