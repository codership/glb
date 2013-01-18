/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_socket_h_
#define _glb_socket_h_

#include <stdbool.h>
#include <string.h> // for memcmp() and stuff
#include <netinet/in.h>

typedef struct sockaddr_in glb_sockaddr_t;

#ifdef GLBD

struct glb_cnf;

extern void
glb_socket_init(const struct glb_cnf* cnf);

#endif /* GLBD */

// behaves like inet_ntoa() - returns static buffer
extern const char*
glb_socket_addr_to_string (const glb_sockaddr_t* addr);

static inline bool
glb_socket_addr_is_equal (const glb_sockaddr_t* left,
                          const glb_sockaddr_t* right)
{
    return (!memcmp(left, right, sizeof (glb_sockaddr_t)));
}

// Initialize glb_sockaddr_t struct
extern long
glb_socket_addr_init (glb_sockaddr_t* addr,
                      const char*     hostname,
                      uint16_t        port);

extern void
glb_socket_addr_set_port (glb_sockaddr_t* addr, uint16_t port);

extern short
glb_socket_addr_get_port (const glb_sockaddr_t* addr);

#define GLB_SOCK_NODELAY      1U
#define GLB_SOCK_DEFER_ACCEPT 2U
#define GLB_SOCK_NONBLOCK     4U
#define GLB_SOCK_KEEPALIVE    8U

// Returns socket (file descriptor) bound to a given address
// with default options set
extern int
glb_socket_create (const glb_sockaddr_t* addr, uint32_t optflags);

#ifdef GLBD

extern uint32_t
glb_socket_addr_hash (const glb_sockaddr_t* addr);

// Sets default socket options
extern int
glb_socket_setopt (int sock, uint32_t optflags);

#endif /* GLBD */

#endif // _glb_socket_h_
