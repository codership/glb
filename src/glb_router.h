/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_router_h_
#define _glb_router_h_

#include "glb_cnf.h"
#include "glb_wdog_backend.h"

typedef struct glb_router glb_router_t;

extern glb_router_t*
glb_router_create (const glb_cnf_t* cnf);

extern void
glb_router_destroy (glb_router_t* router);

/*!
 * Modifies internal list of destinations
 * If destination is not in the list, adds it, if it is there - changes its
 * weight, if weight is -1 - removes destination from the list
 * @return index of record changed or negative error code
 */
extern int
glb_router_change_dst (glb_router_t* router, const glb_dst_t* dst,
                       glb_backend_thread_ctx_t* probe_ctx);

#ifdef GLBD

/*!
 * Finds destination for connection and copies its address to dst_addr.
 * @param src_hint 4-byte hash of client address if available
 * @return 0 if found, -EHOSTDOWN if not
 */
extern int
glb_router_choose_dst (glb_router_t*   const router,
                       uint32_t        const src_hint,
                       glb_sockaddr_t* const dst_addr);

/*!
 * Atomically marks destination referenced by dst_addr as unavailable plus
 * finds new destination and copies its address to dst_addr.
 * @param src_hint 4-byte hash of client address if available
 * @return 0 if found, -EHOSTDOWN if not
 */
extern int
glb_router_choose_dst_again (glb_router_t*   const router,
                             uint32_t        const src_hint,
                             glb_sockaddr_t* const dst_addr);

/*!
 * Returns file descriptor of a new destinaiton conneciton and fills
 * dst_addr with real server address
 *
 * Not thread-safe. Supposed to be called ONLY from the listener main loop.
 *
 * @return 0 or negative error code
 */
extern int
glb_router_connect (glb_router_t* router, const glb_sockaddr_t* src_addr,
                    glb_sockaddr_t* dst_addr, int* sock);

/*!
 * Decrements connection reference count for destination
 */
extern void
glb_router_disconnect (glb_router_t* router, const glb_sockaddr_t* dst_addr,
                       bool failed);

#else /* GLBD */

extern int glb_router_connect(glb_router_t* const router, int const sockfd);

#endif /* GLBD */

// Returns the length of the string
extern size_t
glb_router_print_info (glb_router_t* router, char* buf, size_t buf_len);

#endif // _glb_router_h_
