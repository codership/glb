/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_signal_h_
#define _glb_signal_h_

#include <signal.h>

extern volatile sig_atomic_t
glb_terminate;

extern void
glb_signal_set_handler();

#endif // _glb_signal_h_
