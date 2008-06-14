/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_router_h_
#define _glb_router_h_

#include "glb_dst.h"

typedef struct glb_router glb_router_t;

extern glb_router_t*
glb_router_create (size_t n_dst, glb_dst_t dst[]);

// return file descriptor of a new destinaiton conneciton
extern int
glb_router_connect (glb_router_t* router);

extern void
glb_router_disconnect (glb_router_t* router, int fd);

/*!
 * Modifies internal list of destinations
 * If destination is not in the list, adds it, if it is there - changes its
 * weight, if weight is -1 - removes destination from the list
 * @return index of record changed or negative error code
 */
extern long
glb_router_change_dst (glb_router_t* router, glb_dst_t* dst);

#endif // _glb_router_h_
