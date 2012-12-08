/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_cmd.h"
#include "glb_limits.h"
#include "glb_socket.h"

#include <errno.h>
#include <assert.h>
#include <getopt.h>

typedef struct option option_t;
extern char* optarg;
#define NA no_argument
#define RA required_argument
#define OA optional_argument

typedef enum cmd_opt
{
    CMD_OPT_VERSION      = 'V',
    CMD_OPT_DEFER_ACCEPT = 'a',
    CMD_OPT_ROUND_ROBIN  = 'b',
    CMD_OPT_CONTROL      = 'c',
    CMD_OPT_DAEMON       = 'd',
    CMD_OPT_FIFO         = 'f',
    CMD_OPT_HELP         = 'h',
    CMD_OPT_MAX_CONN     = 'm',
    CMD_OPT_NODELAY      = 'n',
    CMD_OPT_RANDOM       = 'r',
    CMD_OPT_SRC_TRACKING = 's',
    CMD_OPT_N_THREADS    = 't',
    CMD_OPT_VERBOSE      = 'v',
} cmd_opt_t;

static option_t cmd_options[] =
{
    { "version",         NA, NULL, CMD_OPT_VERSION       },
    { "defer-accept",    NA, NULL, CMD_OPT_DEFER_ACCEPT  },
    { "round",           NA, NULL, CMD_OPT_ROUND_ROBIN   },
    { "round-robin",     NA, NULL, CMD_OPT_ROUND_ROBIN   },
    { "rrb",             NA, NULL, CMD_OPT_ROUND_ROBIN   },
    { "control",         RA, NULL, CMD_OPT_CONTROL       },
    { "daemon",          NA, NULL, CMD_OPT_DAEMON        },
    { "fifo",            RA, NULL, CMD_OPT_FIFO          },
    { "help",            NA, NULL, CMD_OPT_HELP          },
    { "max_conn",        RA, NULL, CMD_OPT_MAX_CONN      },
    { "connections",     RA, NULL, CMD_OPT_MAX_CONN      },
    { "nodelay",         NA, NULL, CMD_OPT_NODELAY       },
    { "random",          NA, NULL, CMD_OPT_RANDOM        },
    { "src_tracking",    NA, NULL, CMD_OPT_SRC_TRACKING  },
    { "source_tracking", NA, NULL, CMD_OPT_SRC_TRACKING  },
    { "threads",         RA, NULL, CMD_OPT_N_THREADS     },
    { "verbose",         NA, NULL, CMD_OPT_VERBOSE       },
    { 0, 0, 0, 0 }
};

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
             "  -f|--fifo <fifo name>     name of the FIFO file for control.\n");
    fprintf (out,
             "  -m|--max_conn N           "
             "maximum allowed number of client connections (OS dependent).\n");
    fprintf (out,
             "  -n|--nodelay              "
             "*DISABLE* TCP_NODELAY socket option\n"
             "                            (default: enabled).\n");
    fprintf (out,
             "  -r|--random               "
             "route connections to randomly selected destination.\n");
    fprintf (out,
             "  -s|--source_tracking      "
             "turn on source tracking: route connections from one\n"
             "                            source to the same destination.\n");
    fprintf (out,
             "  -t|--threads N            "
             "number of working threads (connection pools).\n");
    fprintf (out,
             "  -v|--verbose              turn on verbose reporting.\n");
    fprintf (out,
             "  -V|--version              print program version.\n");
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
    exit (EXIT_FAILURE);
}


// Defaults relevant to CLI
static const char cmd_inc_addr_default[]  = "0.0.0.0";
static const char cmd_ctrl_addr_default[] = "127.0.0.1";

glb_cnf_t*
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
    while ((opt = getopt_long (argc, argv, "abc:dfhm:nt:rsvV", cmd_options,
                               &opt_idx)) != -1) {
        switch (opt) {
        case CMD_OPT_DEFER_ACCEPT:
            tmp->defer_accept = true;
            break;
        case CMD_OPT_ROUND_ROBIN:
            tmp->policy = GLB_POLICY_ROUND;
            break;
        case CMD_OPT_CONTROL:
            if (glb_parse_addr (&tmp->ctrl_addr, optarg, cmd_ctrl_addr_default))
                exit (EXIT_FAILURE);
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
                exit (EXIT_FAILURE);
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
            fprintf (stderr, "Option '%s' (%d) not supported yet. Ignoring.\n",
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
    if (glb_parse_addr (&tmp->inc_addr, argv[optind], cmd_inc_addr_default)) {
        exit (EXIT_FAILURE);
    }
    inc_port = glb_socket_addr_get_port (&tmp->inc_addr);

#if 0 // don't open socket by default for security considerations.
    // if control address was not specified
    if (!tmp->ctrl_set) {
        char port[6] = { 0, };
        snprintf (port, 5, "%hu", inc_port + 1);
        if (glb_parse_addr (&tmp->ctrl_addr, port, cmd_ctrl_addr_default))
            exit (EXIT_FAILURE);
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

    return glb_parse_dst_list (dst_list, argc - optind, inc_port, tmp);
}


