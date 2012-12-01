/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_cmd.h"
#include "glb_log.h"
#include "glb_limits.h"
#include "glb_signal.h"
#include "glb_daemon.h"
#include "glb_router.h"
#include "glb_pool.h"
#include "glb_listener.h"
#include "glb_control.h"

#include <unistd.h> // for sleep()

int main (int argc, char* argv[])
{
    glb_router_t*   router;
    glb_pool_t*     pool;
    glb_listener_t* listener;
    glb_ctrl_t*     ctrl;
    uint16_t        inc_port;

    glb_limits_init();

    glb_cmd_parse (argc, argv);
    if (!glb_conf) {
        fprintf (stderr, "Failed to parse arguments. Exiting.\n");
        exit (EXIT_FAILURE);
    }

    glb_cmd_print (stdout, glb_conf);

    if (glb_log_init (GLB_LOG_PRINTF)) {
        fprintf (stderr, "Failed to initialize logger. Aborting.\n");
        exit (EXIT_FAILURE);
    }

    glb_signal_set_handler();

    if (glb_conf->daemonize) {
        glb_daemon_start();
        // at this point we're a child process
    }

    router = glb_router_create (glb_conf->n_dst, glb_conf->dst);
    if (!router) {
        glb_log_fatal ("Failed to create router. Exiting.");
        exit (EXIT_FAILURE);
    }

    pool = glb_pool_create (glb_conf->n_threads, router);
    if (!pool) {
        glb_log_fatal ("Failed to create thread pool. Exiting.");
        exit (EXIT_FAILURE);
    }

    listener = glb_listener_create (&glb_conf->inc_addr, router, pool);
    if (!listener) {
        glb_log_fatal ("Failed to create connection listener. Exiting.");
        exit (EXIT_FAILURE);
    }

    inc_port = glb_socket_addr_get_port (&glb_conf->inc_addr);
    if (glb_conf->ctrl_set) {
        ctrl = glb_ctrl_create (router, pool, inc_port, glb_conf->fifo_name,
                                &glb_conf->ctrl_addr);
    } else {
        ctrl = glb_ctrl_create (router, pool, inc_port, glb_conf->fifo_name,
                                NULL);
    }

    if (!ctrl) {
        glb_log_fatal ("Failed to create control thread. Exiting.");
        exit (EXIT_FAILURE);
    }

    if (glb_conf->daemonize) {
        glb_daemon_ok (); // Tell parent that daemon successfully started
        glb_log_info ("Started.");
    }

    while (!glb_terminate) {

        if (!glb_conf->daemonize) {
            char stats[BUFSIZ];

            glb_router_print_info (router, stats, BUFSIZ);
            puts (stats);

            glb_pool_print_info (pool, stats, BUFSIZ);
            puts (stats);
        }

        sleep (5);
    }

    // cleanup
    glb_ctrl_destroy (ctrl); // removes FIFO file
    // everything else is automatically closed/removed on program exit.

    if (glb_conf->daemonize) {
        glb_log_info ("Exit.");
    }

    return 0;
}
