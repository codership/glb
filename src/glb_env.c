/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#define GLB_CNF_ACCESS

#include "glb_cmd.h"
#include "glb_limits.h"
#include "glb_socket.h"

#include <stddef.h> // ptrdiff_t
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>

// Some constants
#define cmd_ip_len_max     256
#define cmd_port_max       ((1<<16) - 1)

// Defaults relevant to ENV
static const char cmd_ctrl_addr_default[] = "127.0.0.1";

void
glb_cmd_parse (int argc, char* argv[])
{
    glb_cnf_t*   tmp = glb_cnf_init(); // initialize to defaults
    const char** dst_list = NULL;
    int          opt = 0;
    int          opt_idx = 0;
    char*        endptr;
    uint16_t     inc_port;

    if (!tmp) exit (EXIT_FAILURE);

    // parse options
    while ((opt = getopt_long(argc,argv,"ac:dfhm:nt:rsvV",cmd_options,&opt_idx))
           != -1) {
        switch (opt) {
        case CMD_OPT_DEFER_ACCEPT:
            tmp->defer_accept = true;
            break;
        case CMD_OPT_CONTROL:
            if (cmd_parse_addr (&tmp->ctrl_addr, optarg, cmd_ctrl_addr_default))
                return;
            tmp->ctrl_set = true;
            break;
        case CMD_OPT_DAEMON:
            tmp->daemonize = true;
            break;
        case CMD_OPT_FIFO:
            tmp->fifo_name = optarg;
            break;
        case '?':
        case CMD_OPT_HELP:
            glb_cmd_help(stdout, argv[0]);
            exit (EXIT_FAILURE);
            break;
        case CMD_OPT_MAX_CONN:
            tmp->max_conn = strtol (optarg, &endptr, 10);
            if ((*endptr != '\0' && *endptr != ' ') || errno) {
                fprintf (stderr, "Bad max_conn value: %s. Integer expected.\n",
                         optarg);
                return;
            }
            break;
        case CMD_OPT_NODELAY:
            tmp->nodelay = false;
            break;
        case CMD_OPT_N_THREADS:
            tmp->n_threads = strtol (optarg, &endptr, 10);
            if ((*endptr != '\0' && *endptr != ' ') || errno) {
                fprintf (stderr, "Bad n_threads value: %s. Integer expected.\n",
                         optarg);
                exit (EXIT_FAILURE);
            }
            break;
        case CMD_OPT_RANDOM:
            tmp->policy = GLB_POLICY_RANDOM;
            break;
        case CMD_OPT_SRC_TRACKING:
            tmp->policy = GLB_POLICY_SOURCE;
            break;
        case CMD_OPT_VERBOSE:
            tmp->verbose = true;
            break;
        case CMD_OPT_VERSION:
            glb_print_version (stdout);
            if (argc == 2) exit(0);
            break;
        default:
            glb_log_warn ("Option '%s' (%d) not supported yet. Ignoring.\n",
                          cmd_options[opt_idx].name, opt);
        }
    }

    // first non-option argument
    if (optind >= argc) {
        fprintf (stderr, "Missing required argument: LISTEN_ADDR.\n");
        glb_cmd_help(stderr, argv[0]);
        exit (EXIT_FAILURE);
    }

    // parse obligatory incoming address
    if (cmd_parse_addr (&tmp->inc_addr, argv[optind], cmd_inc_addr_default)) {
        return;
    }
    inc_port = glb_socket_addr_get_port (&tmp->inc_addr);

#if 0 // don't open socket by default for security considerations.
    // if control address was not specified
    if (!tmp->ctrl_set) {
        char port[6] = { 0, };
        snprintf (port, 5, "%hu", inc_port + 1);
        if (cmd_parse_addr (&tmp->ctrl_addr, port, cmd_ctrl_addr_default))
            return;
        tmp->ctrl_set = true;
    }
#endif

    // if number of threads was not specified
    if (!tmp->n_threads) tmp->n_threads = 1;

    if (tmp->max_conn > glb_get_conn_limit()) {
        int res = glb_set_conn_limit (tmp->max_conn);
        if (res > 0) tmp->max_conn = res;
    }

    if (tmp->daemonize) tmp->verbose = false;

    // parse destination list
    if (++optind < argc) dst_list = (const char**) &(argv[optind]);
    assert (argc >= optind);

    glb_cnf = cmd_parse_dst_list (dst_list, argc - optind, inc_port, tmp);
}


