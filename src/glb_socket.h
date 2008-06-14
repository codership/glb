/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_socket_h_
#define _glb_socket_h_

// Fill-in struct in_addr given hostname
// @return negative error code in case of failure
int
glb_socket_in_addr (struct in_addr* addr, const char* hostname);

// Fill-in struct sockaddr_in given in_addr and port
void
glb_socket_sockaddr (struct sockaddr_in* name,
                     struct in_addr*     host,
                     uint16_t            port);

// Return socket bound to a given address.
int
glb_socket_make (struct in_addr* addr, uint16_t port);

#endif // _glb_socket_h_
