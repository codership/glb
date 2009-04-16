/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_pool_stats_h_
#define _glb_pool_stats_h_

typedef struct glb_pool_stats
{
    ulong rx_bytes;     // bytes received on incoming interface (from clients)
    ulong tx_bytes;     // bytes transmittted from incoming interface
    ulong recv_bytes;   // bytes received by recv() calls
    ulong n_recv;       // number of recv() calls
    ulong send_bytes;   // bytes sent by send() calls
    ulong n_send;       // number of send() calls
    ulong conns_opened; // number of opened  connections
    ulong conns_closed; // number of closed  connections
    ulong n_conns;      // number of current connections
    ulong poll_reads;   // number of read-ready fd's returned by poll()
    ulong poll_writes;  // number of write-ready fd's returned by poll()
    ulong n_polls;      // number of poll() calls
} glb_pool_stats_t;

static const glb_pool_stats_t glb_zero_stats = { 0, };

// adds right stats to left stats
static inline void
glb_pool_stats_add (glb_pool_stats_t* left, glb_pool_stats_t* right)
{
    left->rx_bytes     += right->rx_bytes;
    left->tx_bytes     += right->tx_bytes;
    left->recv_bytes   += right->recv_bytes;
    left->n_recv       += right->n_recv;
    left->send_bytes   += right->send_bytes;
    left->n_send       += right->n_send;
    left->conns_opened += right->conns_opened;
    left->conns_closed += right->conns_closed;
    left->n_conns      += right->n_conns;
    left->poll_reads   += right->poll_reads;
    left->poll_writes  += right->poll_writes;
    left->n_polls      += right->n_polls;
}

#endif // _glb_pool_stats_h_
