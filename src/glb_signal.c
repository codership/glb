/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_log.h"
#include "glb_signal.h"

#include <stdio.h> // remove()

volatile sig_atomic_t
glb_terminate = 0;

const char*
glb_fifo_name = NULL;

static void
fifo_cleanup()
{
    if (glb_fifo_name) remove (glb_fifo_name);
}

static void
signal_handler(int signum)
{
    switch(signum) {
//    case SIGALRM: should not be getting this signal either
//        break;
    case GLB_SIGNAL_OK: // used by child to report OK
        exit (EXIT_SUCCESS);
    case SIGCHLD:
        fifo_cleanup();
        glb_log_fatal ("Child unexpectedly terminated.");
        exit (EXIT_FAILURE);
    case SIGHUP:
    case SIGTERM:
    case SIGINT:
    case SIGQUIT:
    case SIGPIPE:
        fifo_cleanup();
        glb_log_info ("Received signal %d. Terminating.", signum);
        glb_terminate = 1;
        return;
    default: // should never get here
        fifo_cleanup();
        glb_log_warn ("Received signal %d.", signum);
        abort();
    }
}

void
glb_signal_set_handler()
{
    signal (GLB_SIGNAL_OK, signal_handler);
    signal (SIGCHLD, signal_handler);
    signal (SIGHUP,  signal_handler);
    signal (SIGTERM, signal_handler);
    signal (SIGINT,  signal_handler);
    signal (SIGQUIT, signal_handler);
    signal (SIGPIPE, signal_handler);
}
