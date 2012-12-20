/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_ctrl_h_
#define _glb_ctrl_h_

#include "glb_socket.h"
#include "glb_router.h"
#include "glb_wdog.h"
#include "glb_pool.h"

typedef struct glb_ctrl glb_ctrl_t;

/*!
 * Creates control thread
 * @param cnf
 * @param router
 * @param port
 *        default destination port
 * @param fifo
 *        control fifo descriptor
 * @param sock
 *        socket to listen at for ctrl form another host, may be 0
 */
extern glb_ctrl_t*
glb_ctrl_create (glb_cnf_t*    cnf,
                 glb_router_t* router,
                 glb_pool_t*   pool, // can be NULL
                 glb_wdog_t*   wdog, // can be NULL
                 uint16_t      port,
                 int           fifo,
                 int           sock);

extern void
glb_ctrl_destroy (glb_ctrl_t* ctrl);

#endif // _glb_ctrl_h_
