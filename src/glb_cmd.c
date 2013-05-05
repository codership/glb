/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_cmd.h"
#include "glb_opt.h"
#include "glb_limits.h"
#include "glb_socket.h"

#include <errno.h>
#include <assert.h>
#include <ctype.h> // isspace()

extern char* optarg;

// Defaults relevant to CLI
static const char cmd_inc_addr_default[]  = "0.0.0.0";
static const char cmd_ctrl_addr_default[] = "127.0.0.1";


static void
cmd_parse_options (int argc, char* argv[], glb_cnf_t* cnf)
{
    int   opt = 0;
    int   opt_idx = 0;
    char* endptr;

    // parse options
    while ((opt = getopt_long (argc, argv, "DKL:STVYabc:dfhi:lm:nt:rsvw:x:",
                               glb_options, &opt_idx)) != -1) {
        switch (opt) {
        case GLB_OPT_DISCOVER:
            cnf->discover = true;
            break;
        case GLB_OPT_KEEPALIVE:
            cnf->keepalive = false;
            break;
        case GLB_OPT_LATENCY_COUNT:
            cnf->lat_factor = strtol (optarg, &endptr, 10);
            if ((*endptr != '\0' && !isspace(*endptr)) || errno ||
                cnf->lat_factor < 0) {
                fprintf (stderr, "Bad latency count value: %s. "
                         "Non-negative integer expected.\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;
        case GLB_OPT_SINGLE:
            cnf->policy = GLB_POLICY_SINGLE; // implies GLB_OPT_TOP
        case GLB_OPT_TOP:
            cnf->top = true;
            break;
        case GLB_OPT_VERSION:
            glb_print_version (stdout);
            if (argc == 2) exit(0);
            break;
        case GLB_OPT_SYNCHRONOUS:
            cnf->synchronous = true;
            break;
        case GLB_OPT_DEFER_ACCEPT:
            cnf->defer_accept = true;
            break;
        case GLB_OPT_ROUND_ROBIN:
            cnf->policy = GLB_POLICY_ROUND;
            break;
        case GLB_OPT_CONTROL:
            if (glb_parse_addr (&cnf->ctrl_addr, optarg, cmd_ctrl_addr_default))
                exit (EXIT_FAILURE);
            cnf->ctrl_set = true;
            break;
        case GLB_OPT_DAEMON:
            cnf->daemonize = true;
            break;
        case GLB_OPT_FIFO:
            cnf->fifo_name = optarg;
            break;
        case '?':
        case GLB_OPT_HELP:
            glb_cmd_help(stdout, argv[0]);
            exit (EXIT_FAILURE);
            break;
        case GLB_OPT_INTERVAL:
            cnf->interval = glb_time_from_double(strtod (optarg, &endptr));
            if ((*endptr != '\0' && !isspace(*endptr)) || errno ||
                cnf->interval <= 0) {
                fprintf (stderr, "Bad check interval value: %s. "
                         "Positive real number expected.\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;
        case GLB_OPT_LINGER:
            cnf->linger = true;
            break;
        case GLB_OPT_MAX_CONN:
            cnf->max_conn = strtol (optarg, &endptr, 10);
            if ((*endptr != '\0' && !isspace(*endptr)) || errno) {
                fprintf (stderr, "Bad max_conn value: %s. Integer expected.\n",
                         optarg);
                exit (EXIT_FAILURE);
            }
            break;
        case GLB_OPT_NODELAY:
            cnf->nodelay = false;
            break;
        case GLB_OPT_N_THREADS:
            cnf->n_threads = strtol (optarg, &endptr, 10);
            if ((*endptr != '\0' && !isspace(*endptr)) || errno) {
                fprintf (stderr, "Bad n_threads value: %s. Integer expected.\n",
                         optarg);
                exit (EXIT_FAILURE);
            }
            break;
        case GLB_OPT_RANDOM:
            cnf->policy = GLB_POLICY_RANDOM;
            break;
        case GLB_OPT_SRC_TRACKING:
            cnf->policy = GLB_POLICY_SOURCE;
            break;
        case GLB_OPT_VERBOSE:
            cnf->verbose = true;
            break;
        case GLB_OPT_WATCHDOG:
            cnf->watchdog = optarg;
            break;
        case GLB_OPT_EXTRA_POLLS:
            cnf->extra = glb_time_from_double(strtod (optarg, &endptr));
            if ((*endptr != '\0' && !isspace(*endptr)) || errno ||
                cnf->extra < 0) {
                fprintf (stderr, "Bad extra value: %s. "
                         "Non-negative real number expected.\n", optarg);
                exit (EXIT_FAILURE);
            }
            break;
        default:
            fprintf (stderr, "Option '%s' (%d) not supported yet. Ignoring.\n",
                     glb_options[opt_idx].name, opt);
        }
    }
}

void
glb_cmd_help (FILE* out, const char* progname)
{
    fprintf (out,
             "Usage:\n  %s [OPTIONS] LISTEN_ADDRESS "
             "[DESTINATION_LIST]\nOPTIONS:\n", progname);
    fprintf (out,
             "  -h|--help                 this help message.\n");
    fprintf (out,
             "  -a|--defer-accept         "
             "enable TCP_DEFER_ACCEPT on the listening socket\n"
             "                            (default: disabled).\n");
    fprintf (out,
             "  -b|--round                "
             "round-robin destination selection policy.\n");
    fprintf (out,
             "  -c|--control [HOST:]PORT  "
             "listen for control requests on this address.\n");
    fprintf (out,
             "  -d|--daemon               run as a daemon.\n");
    fprintf (out,
             "  -f|--fifo <fifo name>     "
             "name of the FIFO file for control.\n");
    fprintf (out,
             "  -i|--interval D.DDD       "
             "how often to probe destinations for liveness\n"
             "                            (fractional seconds, default 1.0).\n"
        );
    fprintf (out,
             "  -l|--linger               "
             "*DISABLE* sockets lingering in TIME_WAIT state after\n"
             "                            close().\n");
    fprintf (out,
             "  -m|--max_conn N           "
             "maximum allowed number of client connections\n"
             "                            (OS dependent).\n");
    fprintf (out,
             "  -n|--nodelay              "
             "*DISABLE* TCP_NODELAY socket option\n"
             "                            (default: enabled).\n");
    fprintf (out,
             "  -r|--random               "
             "route connections to randomly selected destination.\n");
    fprintf (out,
             "  -s|--source               "
             "turn on source tracking: route connections from one\n"
             "                            source to the same destination.\n");
    fprintf (out,
             "  -t|--threads N            "
             "number of working threads (connection pools).\n");
    fprintf (out,
             "  -v|--verbose              turn on verbose reporting.\n");
    fprintf (out,
             "  -w|--watchdog SPEC_STR    watchdog specification.\n");
    fprintf (out,
             "  -x|--extra D.DDD          "
             "perform extra destination poll on connection attempt\n"
             "                            "
             "if the previous poll happened more than D.DD seconds\n"
             "                            ago.\n"
             "                            "
             "(default: 0.0 - extra polling disabled)\n");
    fprintf (out,
             "  -D|--discover             "
             "use watchdog results to discover and set new\n"
             "                            destinations.\n"
             "                            "
             "(Currently only Galera nodes supply such info.)\n");
    fprintf (out,
             "  -K|--keepalive            "
             "*DISABLE* SO_KEEPALIVE socket option on server-side\n"
             "                            sockets (default: enabled).\n");
    fprintf (out,
             "  -L|--latency <samples>    "
             "when using latency reported by watchdog probes for\n"
             "                            "
             "destination weight adjustment, how many samples to\n"
             "                            average latency over.\n"
             "                            "
             "(default: 0 - not using reported latency for weight\n"
             "                            adjustment)\n");
    fprintf (out,
             "  -S|--single               "
             "direct all connections to a single destination\n"
             "                            "
             "with top weight.\n");
    fprintf (out,
             "  -T|--top                  "
             "balance only between destinations with top weight.\n");
    fprintf (out,
             "  -V|--version              print program version.\n");
    fprintf (out,
             "  -Y                        "
             "connect synchronously (one-at-a-time).\n");
    fprintf (out, "LISTEN_ADDRESS:\n"
             "  [IP:]PORT                 "
             "where to listen for incoming TCP connections at.\n"
             "                            "
             "(without IP part - bind to all interfaces)\n"
             );
    fprintf (out, "DESTINATION_LIST:\n"
             "  [H1[:P1[:W1]]] [H2[:P2[:W2]]]... "
             " - a space-separated list of destinations\n"
             "                            in the form address:port:weight.\n");
    fprintf (out, "SPEC_STR:\n"
             "  BACKEND_ID[:BACKEND_SPECIFIC_STRING], "
             "e.g. exec:'<command line>'\n");
    exit (EXIT_FAILURE);
}


glb_cnf_t*
glb_cmd_parse (int argc, char* argv[])
{
    glb_cnf_t*   cnf = glb_cnf_init(); // initialize to defaults
    const char** dst_list = NULL;
    uint16_t     inc_port;

    if (!cnf) exit (EXIT_FAILURE);

    // parse options
    cmd_parse_options (argc, argv, cnf);

    // first non-option argument
    if (optind >= argc) {
        fprintf (stderr, "Missing required argument: LISTEN_ADDR.\n");
        glb_cmd_help(stderr, argv[0]);
        exit (EXIT_FAILURE);
    }

    // parse obligatory incoming address
    if (glb_parse_addr (&cnf->inc_addr, argv[optind], cmd_inc_addr_default)) {
        exit (EXIT_FAILURE);
    }
    inc_port = glb_sockaddr_get_port (&cnf->inc_addr);

#if 0 // don't open socket by default for security considerations.
    // if control address was not specified
    if (!cnf->ctrl_set) {
        char port[6] = { 0, };
        snprintf (port, 5, "%hu", inc_port + 1);
        if (glb_parse_addr (&cnf->ctrl_addr, port, cmd_ctrl_addr_default))
            exit (EXIT_FAILURE);
        cnf->ctrl_set = true;
    }
#endif

    // if number of threads was not specified
    if (!cnf->n_threads) cnf->n_threads = 1;

    if (cnf->max_conn > glb_get_conn_limit()) {
        int res = glb_set_conn_limit (cnf->max_conn);
        if (res > 0) cnf->max_conn = res;
    }

    if (cnf->daemonize) cnf->verbose = false;

    // parse destination list
    if (++optind < argc) dst_list = (const char**) &(argv[optind]);
    assert (argc >= optind);

    return glb_parse_dst_list (dst_list, argc - optind, inc_port, cnf);
}


