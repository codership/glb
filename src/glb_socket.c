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
#include <assert.h>
#include <unistd.h>

#include "glb_log.h"
#include "glb_socket.h"

//static const size_t addr_string_len = 512; heh, my GCC refuses to see it as
//a constant! here goes type safety...
#define addr_string_len 512
static char addr_string[addr_string_len] = { 0, };

// maximum IP address length = 21: aaa.bbb.ccc.ddd:ppppp
//                                                ^ position 15
const char*
glb_socket_addr_to_string (const glb_sockaddr_t* addr)
{
    uint8_t* a = (void*)&addr->sin_addr.s_addr;
    char ip[16];
    long ip_len;
    char port[7];
    long port_len;

    ip_len = snprintf (ip, 16, "%hhu.%hhu.%hhu.%hhu", a[0], a[1], a[2], a[3]);
    assert (ip_len < 16);

    port_len = snprintf (port, 7, ":%hu", ntohs (addr->sin_port));
    assert (port_len < 7);

    snprintf (addr_string, addr_string_len, "                     ");

    // make so that ':' is on position 15
    memcpy (addr_string + 15 - ip_len, ip, ip_len);
    memcpy (addr_string + 15, port, port_len);

    return addr_string;
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
        glb_log_error ("Unknown host %s.\n", hostname);
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
glb_socket_create (const struct sockaddr_in* addr)
{
    int sock;
//    size_t buf_size = 1024;

    /* Create the socket. */
    sock = socket (PF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        glb_log_error ("Failed to create listening socket: %d (%s)",
                       errno, strerror (errno));
        return -errno;
    }
#if 0
    /* probably a good place to specify some socket options */
    if (setsockopt (sock, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size))) {
        glb_log_error ("setsockopt() failed: %d (%s)", -errno, strerror(errno));
        close (sock);
        return -errno;
    }

    if (setsockopt (sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size))) {
        glb_log_error ("setsockopt() failed: %d (%s)", -errno, strerror(errno));
        close (sock);
        return -errno;
    }
#endif
    /* Give the socket a name. */
    if (bind (sock, (struct sockaddr *) addr, sizeof (*addr)) < 0)
    {
        glb_log_error ("Failed to bind listening socket: %d (%s)",
                       errno, strerror (errno));
        close (sock);
        return -errno;
    }

    return sock;
}

