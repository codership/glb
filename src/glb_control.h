/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_ctrl_h_
#define _glb_ctrl_h_

#include "glb_socket.h"
#include "glb_router.h"

typedef struct glb_ctrl glb_ctrl_t;

/*!
 * Creates control thread
 * @param router
 * @param unix_name
 *        unix socket name
 * @param inet_addr
 *        interface address for ctrl form another host, may be NULL
 */
extern glb_ctrl_t*
glb_ctrl_create (glb_router_t*         router,
                 const char*           fifo_name,
                 const glb_sockaddr_t* inet_addr);

extern void
glb_ctrl_destroy (glb_ctrl_t* ctrl);

#endif // _glb_ctrl_h_
