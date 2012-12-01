/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#define GLB_CMD_C

#include "../config.h" // for version

#include "glb_cmd.h"
#include "glb_log.h"
#include "glb_limits.h"
#include "glb_socket.h"

glb_cmd_t* glb_conf = NULL;

#include <stddef.h> // ptrdiff_t
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <getopt.h>
#include <arpa/inet.h>

typedef struct option option_t;
extern char* optarg;
#define NA no_argument
#define RA required_argument
#define OA optional_argument

typedef enum cmd_opt
{
    CMD_OPT_VERSION      = 'V',
    CMD_OPT_CONTROL      = 'c',
    CMD_OPT_DAEMON       = 'd',
    CMD_OPT_FIFO         = 'f',
    CMD_OPT_HELP         = 'h',
    CMD_OPT_MAX_CONN     = 'm',
    CMD_OPT_NODELAY      = 'n',
    CMD_OPT_SRC_TRACKING = 's',
    CMD_OPT_N_THREADS    = 't',
    CMD_OPT_VERBOSE      = 'v',
} cmd_opt_t;

static option_t cmd_options[] =
{
    { "version",         NA, NULL, CMD_OPT_VERSION       },
    { "control",         RA, NULL, CMD_OPT_CONTROL       },
    { "daemon",          NA, NULL, CMD_OPT_DAEMON        },
    { "fifo",            RA, NULL, CMD_OPT_FIFO          },
    { "help",            NA, NULL, CMD_OPT_HELP          },
    { "max_conn",        RA, NULL, CMD_OPT_MAX_CONN      },
    { "connections",     RA, NULL, CMD_OPT_MAX_CONN      },
    { "nodelay",         NA, NULL, CMD_OPT_NODELAY       },
    { "src_tracking",    NA, NULL, CMD_OPT_SRC_TRACKING  },
    { "source_tracking", NA, NULL, CMD_OPT_SRC_TRACKING  },
    { "threads",         RA, NULL, CMD_OPT_N_THREADS     },
    { "verbose",         NA, NULL, CMD_OPT_VERBOSE       },
    { 0, 0, 0, 0 }
};

// Some constants
#define cmd_ip_len_max     256
#define cmd_port_max       ((1<<16) - 1)

void
glb_cmd_help (FILE* out, const char* progname)
{
    fprintf (out,
             "Usage:\n  %s [OPTIONS] LISTEN_ADDRESS "
             "[DESTINATION_LIST]\nOPTIONS:\n", progname);
    fprintf (out,
             "  --help                  this help message.\n");
    fprintf (out,
             "  --daemon                run as a daemon.\n");
    fprintf (out,
             "  --fifo <fifo name>      name of the FIFO file for control.\n");
    fprintf (out,
             "  --control [HOST:]PORT   "
             "listen for control requests on this address.\n");
    fprintf (out,
             "  --threads N             "
             "number of working threads (connection pools).\n");
    fprintf (out,
             "  --max_conn N            "
             "maximum allowed number of client connections (OS dependent).\n");
    fprintf (out,
             "  --nodelay               "
             "*DISABLE* TCP_NODELAY socket option (default: enabled).\n");
    fprintf (out,
             "  --source_tracking       "
             "turn on source tracking: route connections from one\n"
             "                          source to the same destination.\n");
    fprintf (out,
             "  --verbose               turn on verbose reporting.\n");
    fprintf (out,
             "  --version               print program version.\n");
    fprintf (out, "LISTEN_ADDRESS:\n"
             "  [IP:]PORT               "
             "where to listen for incoming TCP connections at.\n"
             "                          "
             "(without IP part - bind to all interfaces)\n"
             );
    fprintf (out, "DESTINATION_LIST:\n"
             "  [H1[:P1[:W1]]] [H2[:P2[:W2]]]... "
             " - a space-separated list of destinations\n"
             "                          in the form address:port:weight.\n");
    exit (EXIT_FAILURE);
}

void
glb_cmd_print (FILE* out, const glb_cmd_t* cmd)
{
    ulong i;

    fprintf (out, "Incoming address: %s, ",
             glb_socket_addr_to_string (&cmd->inc_addr));
    fprintf (out, "control FIFO: %s\n", cmd->fifo_name);
    fprintf (out, "Control  address:  %s\n",
             cmd->ctrl_set ? glb_socket_addr_to_string (&cmd->ctrl_addr) :
             "none");
    fprintf (out, "Number of threads: %ld, max connections: %ld, nodelay: %s, "
             "source tracking: %s, verbose: %s, daemon: %s\n",
             cmd->n_threads,
             cmd->max_conn,
             cmd->nodelay ? "ON" : "OFF",
             cmd->src_tracking ? "ON" : "OFF",
             cmd->verbose ? "ON" : "OFF",
             cmd->daemonize ? "YES" : "NO");
    fprintf (out, "Destinations: %lu\n", (ulong)cmd->n_dst);

    for (i = 0; i < cmd->n_dst; i++) {
        char tmp[256];
        glb_dst_print (tmp, sizeof(tmp), &cmd->dst[i]);
        fprintf (out, "  %2lu: %s\n", i, tmp);
    }
}

// parses [addr:]port
static long
cmd_parse_addr (glb_sockaddr_t* addr,
                const char*     str,
                const char*     default_addr)
{
    const char* port_str;
    ulong       port;
    char*       endptr;
    char        addr_str[cmd_ip_len_max + 1] = { 0, };

    port_str = strchr (str, ':');
    if (!port_str) {
        // no separator - only port present
        port_str = str;
        strncpy (addr_str, default_addr, cmd_ip_len_max); // any address
    }
    else {
        ptrdiff_t addr_len = port_str - str;
        if (addr_len > cmd_ip_len_max) {
            fprintf (stderr, "Host address too long: %s\n", str);
            return -EINVAL;
        }
        port_str = port_str + 1;
        strncpy (addr_str, str, addr_len);
//        if (glb_socket_in_addr (addr, addr_str)) {
//            fprintf (stderr, "Invalid host address: %s\n", addr_str);
//            return -EINVAL;
//        }
    }

    port = strtoul (port_str, &endptr, 10);
    if (*endptr != '\0' || port > cmd_port_max) {
        fprintf (stderr, "Invalid port spec: %s\n", port_str);
        return -EINVAL;
    }

//    printf ("Option: %s, found addr = '%s', port = '%s'\n",
//            str, addr_str, port_str);
    return glb_socket_addr_init (addr, addr_str, port);
}

// parses array list of destinations
static glb_cmd_t*
cmd_parse_dst_list (const char* dst_list[],
                    size_t      n_dst,
                    ulong       default_port)
{
    glb_cmd_t*   ret   = NULL;
    size_t       i;

    ret = calloc (sizeof(*ret) + n_dst * sizeof(glb_dst_t), 1);
    if (ret) {
        for (i = 0; i < n_dst; i++) {
            switch (glb_dst_parse (&ret->dst[i], dst_list[i], default_port)) {
            case 1:
                // default port is assigned glb_dst_parse()
            case 2:
                // default weight is assigned glb_dst_parse()
            case 3:
                break;
            default: // error parsing destination
                fprintf (stderr, "Invalid destination spec: %s\n", dst_list[i]);
                free (ret);
                return NULL;
            }
        }
        ret->n_dst = n_dst;
    }
    return ret;
}

// General defaults
static const char  cmd_inc_addr_default[]   = "0.0.0.0";
static const char  cmd_ctrl_addr_default[]  = "127.0.0.1";
static const char  cmd_fifo_name_default[]  = "/tmp/glbd.fifo";
static const ulong cmd_min_threads          = 1;

void
glb_cmd_parse (int argc, char* argv[])
{
    glb_cmd_t    tmp = {{ 0 }}; // initialize to 0
    const char** dst_list = NULL;
    long         opt = 0;
    int          opt_idx = 0;
    char*        endptr;
    uint16_t     inc_port;

    // Set defaults
    tmp.ctrl_set     = false;
    tmp.fifo_name    = cmd_fifo_name_default;
    tmp.n_threads    = cmd_min_threads;
    tmp.max_conn     = glb_max_conn;
    tmp.nodelay      = true;
    tmp.src_tracking = false;
    tmp.verbose      = false;
    tmp.daemonize    = false;

    // parse options
    while ((opt = getopt_long(argc, argv, "c:dfhnm:t:svV",cmd_options,&opt_idx))
           != -1) {
        switch (opt) {
        case '?':
        case CMD_OPT_HELP:
            glb_cmd_help(stdout, argv[0]);
            exit (-1);
            break;
        case CMD_OPT_CONTROL:
            if (cmd_parse_addr (&tmp.ctrl_addr, optarg, cmd_ctrl_addr_default))
                return;
            tmp.ctrl_set = true;
            break;
        case CMD_OPT_FIFO:
            tmp.fifo_name = optarg;
            break;
        case CMD_OPT_NODELAY:
            tmp.nodelay = false;
            break;
        case CMD_OPT_N_THREADS:
            tmp.n_threads = strtol (optarg, &endptr, 10);
            if ((*endptr != '\0' && *endptr != ' ') || errno) {
                fprintf (stderr, "Bad n_threads value: %s. Integer expected.\n",
                         optarg);
                return;
            }
            if (tmp.n_threads<cmd_min_threads) tmp.n_threads = cmd_min_threads;
            break;
        case CMD_OPT_MAX_CONN:
            tmp.max_conn = strtol (optarg, &endptr, 10);
            if ((*endptr != '\0' && *endptr != ' ') || errno) {
                fprintf (stderr, "Bad max_conn value: %s. Integer expected.\n",
                         optarg);
                return;
            }
            break;
        case CMD_OPT_VERBOSE:
            tmp.verbose = true;
            break;
        case CMD_OPT_VERSION:
            printf ("%s v%s (%s)\n", PACKAGE, VERSION,
#if defined(USE_EPOLL)
                    "epoll"
#elif defined(USE_POLL)
                    "poll"
#else
#error "USE_POLL/USE_EPOLL undefined"
#endif
            );
            break;
        case CMD_OPT_DAEMON:
            tmp.daemonize = true;
            break;
        case CMD_OPT_SRC_TRACKING:
            tmp.src_tracking = true;
            break;
        default:
            glb_log_warn ("Option '%s' (%d) not supported yet. Ignoring.\n",
                          cmd_options[opt_idx].name, opt);
        }
    }

    // first non-option argument
    if (optind >= argc) {
        fprintf (stderr, "Missing required argument: LISTEN_ADDR.\n");
        return;
    }

    // parse obligatory incoming address
    if (cmd_parse_addr (&tmp.inc_addr, argv[optind], cmd_inc_addr_default)) {
        return;
    }
    inc_port = glb_socket_addr_get_port (&tmp.inc_addr);

#if 0 // don't open socket by default for security considerations.
    // if control address was not specified
    if (!tmp.ctrl_set) {
        char port[6] = { 0, };
        snprintf (port, 5, "%hu", inc_port + 1);
        if (cmd_parse_addr (&tmp.ctrl_addr, port, cmd_ctrl_addr_default))
            return;
        tmp.ctrl_set = true;
    }
#endif

    // if number of threads was not specified
    if (!tmp.n_threads) tmp.n_threads = 1;

    if (tmp.max_conn > glb_max_conn) {
        fprintf (stderr, "Can't set connection limit to %ld: system limit: %d."
                 " Using system limit instead.\n", tmp.max_conn, glb_max_conn);
        tmp.max_conn = glb_max_conn;
    }

    // parse destination list
    if (++optind < argc) dst_list = (const char**) &(argv[optind]);
    assert (argc >= optind);
    glb_conf = cmd_parse_dst_list (dst_list, argc - optind, inc_port);

    if (tmp.daemonize) tmp.verbose = false;

    if (glb_conf) {
        glb_conf->inc_addr  = tmp.inc_addr;
        glb_conf->ctrl_addr = tmp.ctrl_addr;
        glb_conf->ctrl_set  = tmp.ctrl_set;
        glb_conf->fifo_name = tmp.fifo_name;
        glb_conf->n_threads = tmp.n_threads;
        glb_conf->max_conn  = tmp.max_conn;
        glb_conf->verbose   = tmp.verbose;
        glb_conf->daemonize = tmp.daemonize;
        glb_conf->nodelay   = tmp.nodelay;
        glb_conf->src_tracking = tmp.src_tracking;
    }
}


