/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_signal_h_
#define _glb_signal_h_

#ifdef GLBD

#include <signal.h>

extern volatile sig_atomic_t
glb_terminate;

// The signal to be sent to parent to confirm that child has started fine.
#define GLB_SIGNAL_OK SIGUSR1

extern void
glb_signal_set_handler();

#endif /* GLBD */

#endif // _glb_signal_h_
