/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_ctrl_h_
#define _glb_ctrl_h_

#include "glb_socket.h"
#include "glb_router.h"
#ifdef GLBD
#include "glb_pool.h"
#endif /* GLBD */

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
#ifdef GLBD
                 glb_pool_t*   pool,
#endif /* GLBD */
                 uint16_t      port,
                 int           fifo,
                 int           sock);

extern void
glb_ctrl_destroy (glb_ctrl_t* ctrl);

#endif // _glb_ctrl_h_
