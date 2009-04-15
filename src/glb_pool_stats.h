/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_pool_stats_h_
#define _glb_pool_stats_h_

typedef struct glb_pool_stats
{
    ulong recv_bytes;
    ulong n_recv;
    ulong send_bytes;
    ulong n_send;
    ulong conns_opened;
    ulong conns_closed;
    ulong poll_reads;
    ulong poll_writes;
    ulong n_polls;
} glb_pool_stats_t;

static const glb_pool_stats_t glb_zero_stats = { 0, };

// adds right stats to left stats
static inline void
glb_pool_stats_add (glb_pool_stats_t* left, glb_pool_stats_t* right)
{
    left->recv_bytes   += right->recv_bytes;
    left->n_recv       += right->n_recv;
    left->send_bytes   += right->send_bytes;
    left->n_send       += right->n_send;
    left->conns_opened += right->conns_opened;
    left->conns_closed += right->conns_closed;
    left->poll_reads   += right->poll_reads;
    left->poll_writes  += right->poll_writes;
    left->n_polls      += right->n_polls;
}

#endif // _glb_pool_stats_h_
