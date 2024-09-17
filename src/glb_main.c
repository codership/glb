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
#include "glb_wdog.h"
#include "glb_pool.h"
#include "glb_listener.h"
#include "glb_control.h"
#include "glb_misc.h"

#include <unistd.h>    // for sleep()
#include <sys/types.h>
#include <sys/stat.h>  // for mkfifo()
#include <fcntl.h>     // for open()
#include <errno.h>

/* this function is to allocate all possible resources before dropping
 * privileges */
static int
allocate_resources(const glb_cnf_t* conf,
                   int* ctrl_fifo,
                   int* ctrl_sock,
                   int* listen_sock)
{
    if (mkfifo (conf->fifo_name, S_IRUSR | S_IWUSR)) {
        switch (errno)
        {
        case EEXIST:
            glb_log_error ("FIFO '%s' already exists. Check that no other "
                           "glbd instance is running and delete it "
                           "or specify another name with --fifo option.",
                           conf->fifo_name);
            break;
        default:
            glb_log_error ("Could not create FIFO '%s': %d (%s)",
                           conf->fifo_name, errno, strerror(errno));

        }
        return 1;
    }

    *ctrl_fifo = open (conf->fifo_name, O_RDWR);
    if (*ctrl_fifo < 0) {
        int err = -(*ctrl_fifo);
        glb_log_error ("Ctrl: failed to open FIFO file: %d (%s)",
                       err, strerror (err));
        goto cleanup1;
    }

    if (conf->ctrl_set) {
        *ctrl_sock = glb_socket_create (&conf->ctrl_addr,GLB_SOCK_DEFER_ACCEPT);
        if (*ctrl_sock < 0) {
            int err = -(*ctrl_sock);
            glb_log_error ("Ctrl: failed to create listening socket: %d (%s)",
                           err, strerror (err));
            goto cleanup2;
        }
    }

    *listen_sock = glb_socket_create (&conf->inc_addr, GLB_SOCK_DEFER_ACCEPT);
    if (*listen_sock < 0) {
        int err = -(*listen_sock);
        glb_log_error ("Failed to create listening socket: %d (%s)",
                       err, strerror (err));
        goto cleanup3;
    }

    if (conf->daemonize) { // make sure those survive fork()
        glb_fd_setfd (*ctrl_fifo,   FD_CLOEXEC, false);
        glb_fd_setfd (*ctrl_sock,   FD_CLOEXEC, false);
        glb_fd_setfd (*listen_sock, FD_CLOEXEC, false);
    }

    glb_fifo_name = conf->fifo_name;

    return 0;

cleanup3:
    close (*ctrl_sock);
    *ctrl_sock = 0;
cleanup2:
    close (*ctrl_fifo);
    *ctrl_fifo = 0;
cleanup1:
    remove (conf->fifo_name);

    return 1;
}

static void
free_resources (const char* const fifo_name,
                int const ctrl_fifo,
                int const ctrl_sock,
                int const lsock)
{
    if (lsock) close (lsock);
    if (ctrl_sock) close (ctrl_sock);
    if (ctrl_fifo) {
        close (ctrl_fifo);
        remove (fifo_name);
    }
}

int main (int argc, char* argv[])
{
    bool success             = false;
    glb_router_t*   router   = NULL;
    glb_pool_t*     pool     = NULL;
    glb_listener_t* listener = NULL;
    glb_wdog_t*     wdog     = NULL;
    glb_ctrl_t*     ctrl     = NULL;
    uint16_t        inc_port;

    int listen_sock, ctrl_fifo, ctrl_sock = 0;

    glb_limits_init();

    glb_cnf_t* const cnf = glb_cmd_parse (argc, argv);
    if (!cnf) {
        fprintf (stderr, "Failed to parse arguments. Exiting.\n");
        exit (EXIT_FAILURE);
    }

    if (cnf->verbose) {
        glb_cnf_print (stdout, cnf);
    }
    glb_socket_init (cnf);

    if (glb_log_init (GLB_LOG_STDERR, cnf->verbose)) {
        fprintf (stderr, "Failed to initialize logger. Aborting.\n");
        exit (EXIT_FAILURE);
    }

    if (allocate_resources (cnf, &ctrl_fifo, &ctrl_sock, &listen_sock)) {
        glb_log_fatal ("Failed to allocate inital resources. Aborting.\n");
        exit (EXIT_FAILURE);
    }

    glb_signal_set_handler();

    if (cnf->daemonize) {
        glb_daemon_start (cnf);
        /* at this point we're a child process:
         * 1) make at least those sockets unforkable */
        glb_fd_setfd (ctrl_fifo,   FD_CLOEXEC, true);
        glb_fd_setfd (ctrl_sock,   FD_CLOEXEC, true);
        glb_fd_setfd (listen_sock, FD_CLOEXEC, true);
    }
    /*     2) remove SIGCHLD handler */
    signal (SIGCHLD, SIG_DFL);

    router = glb_router_create (cnf);
    if (!router) {
        glb_log_fatal ("Failed to create router. Exiting.");
        goto cleanup;
    }

    pool = glb_pool_create (cnf, router);
    if (!pool) {
        glb_log_fatal ("Failed to create thread pool. Exiting.");
        goto cleanup;
    }

    if (cnf->watchdog) {
        wdog = glb_wdog_create (cnf, router, pool);
        if (!wdog) {
            glb_log_fatal ("Failed to create destination watchdog. Exiting.");
            goto cleanup;
        }
    }

    inc_port = glb_sockaddr_get_port (&cnf->inc_addr);
    ctrl = glb_ctrl_create (cnf, router, pool, wdog,
                            inc_port, ctrl_fifo, ctrl_sock);
    if (!ctrl) {
        glb_log_fatal ("Failed to create control thread. Exiting.");
        goto cleanup;
    }

    listener = glb_listener_create (cnf, router, pool, listen_sock);
    if (!listener) {
        glb_log_fatal ("Failed to create connection listener. Exiting.");
        goto cleanup;
    }

    if (cnf->daemonize) {
        glb_daemon_ok (); // Tell parent that daemon successfully started
        glb_log_info ("Started.");
    }

    success = true;

    while (!glb_terminate) {

        if (cnf->verbose && !cnf->daemonize) {
            char stats[BUFSIZ];

            if (wdog)
            {
                glb_wdog_print_info (wdog, stats, BUFSIZ);
                puts (stats);
            }

            glb_router_print_info (router, stats, BUFSIZ);
            puts (stats);

            glb_pool_print_info (pool, stats, BUFSIZ);
            puts (stats);
        }

        sleep (5);
    }

cleanup:

    glb_log_debug ("Cleanup on %s.", success ? "shutdown" : "failure");

    if (listener) glb_listener_destroy (listener);
    if (ctrl)     glb_ctrl_destroy     (ctrl);
    if (wdog)     glb_wdog_destroy     (wdog);
    if (pool)     glb_pool_destroy     (pool);
    if (router)   glb_router_destroy   (router);

    if (cnf->daemonize) {
        glb_log_info ("Exit.");
    }

    free_resources (cnf->fifo_name, ctrl_fifo, ctrl_sock, listen_sock);
    free (cnf);

    if (success)
        exit (EXIT_SUCCESS);
    else
        exit (EXIT_FAILURE);
}
