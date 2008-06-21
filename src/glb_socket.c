/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>  // for inet_ntoa()
#include <netdb.h>
#include <errno.h>

#include "glb_socket.h"

//static const size_t addr_string_len = 512; heh, my GCC refuses to see it as a constant! here goes type safety...
#define addr_string_len 512
static char addr_string[addr_string_len] = { 0, };

const char*
glb_socket_addr_to_string (glb_sockaddr_t* addr)
{
    snprintf (addr_string, addr_string_len, "%s:%hu",
              inet_ntoa (addr->sin_addr), ntohs(addr->sin_port));
    return addr_string;
}

int
_glb_socket_in_addr (struct in_addr* addr, const char* hostname)
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
_glb_socket_sockaddr_in (struct sockaddr_in* addr,
                        struct in_addr*     host,
                        uint16_t            port)
{
    addr->sin_family = AF_INET;
    addr->sin_port   = htons (port);
    addr->sin_addr   = *host;
}

// Initialize glb_sockaddr_t struct
long
glb_socket_addr_init (glb_sockaddr_t* addr,
                      const char*     hostname,
                      uint16_t        port)
{
    struct hostent *host;

    host = gethostbyname (hostname);
    if (host == NULL)
    {
        fprintf (stderr, "Unknown host %s.\n", hostname);
        return -EINVAL;
    }
    addr->sin_addr = *(struct in_addr *) host->h_addr;
    addr->sin_port   = htons (port);
    addr->sin_family = AF_INET;

    return 0;
}

void
glb_socket_addr_set_port (glb_sockaddr_t* addr, uint16_t port)
{
    addr->sin_port   = htons (port);
}

short
glb_socket_addr_get_port (glb_sockaddr_t* addr)
{
    return ntohs (addr->sin_port);
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

