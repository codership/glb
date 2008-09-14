/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

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

#include "../config.h" // for version
#include "glb_log.h"
#include "glb_cmd.h"
#include "glb_socket.h"

extern bool glb_verbose;

typedef enum cmd_opt
{
    CMD_OPT_VERSION      = 'V',
    CMD_OPT_CONTROL      = 'c',
    CMD_OPT_DAEMON       = 'd',
    CMD_OPT_FIFO         = 'f',
    CMD_OPT_HELP         = 'h',
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
    { "src_tracking",    NA, NULL, CMD_OPT_SRC_TRACKING  },
    { "source_tracking", NA, NULL, CMD_OPT_SRC_TRACKING  },
    { "threads",         RA, NULL, CMD_OPT_N_THREADS     },
    { "verbose",         NA, NULL, CMD_OPT_VERBOSE       },
    { 0, 0, 0, 0 }
};

// Some constants
static const int  cmd_list_separator = ',';
static const long cmd_ip_len_max     = 256;
static const long cmd_port_max       = (1<<16) - 1;

void
glb_cmd_help (FILE* out, const char* progname)
{
    fprintf (out,
             "Usage:\n  %s [OPTIONS] LISTEN_ADDRESS "
             "[DESTINATION_LIST]\nOPTIONS:\n", progname);
    fprintf (out,
             "  --help                  this help message\n");
    fprintf (out,
             "  --fifo <fifo name>      name of the FIFO file for control\n");
    fprintf (out,
             "  --control [HOST:]PORT   "
             "listen for control requests on this address\n"
             "                          "
             "(default: 127.0.0.1:<LISTEN_PORT + 1>\n");
    fprintf (out,
             "  --threads N             "
             "number of working threads (number of CPU cores)\n");
    fprintf (out,
             "  --source_tracking       "
             "turn on source tracking: route connections from one\n"
             "                          source to the same destination\n");
    fprintf (out,
             "  --verbose               turn on verbose reporting\n");
    fprintf (out,
             "  --version               print program version\n");
    fprintf (out, "LISTEN_ADDRESS:\n"
             "  [IP:]PORT               "
             "where to listen for incoming TCP connections\n");
    fprintf (out, "DESTINATION_LIST:\n"
             "  [H1[:P1[:W1]]] [H2[:P2[:W2]]]... "
             " - a space-separated list of destinations\n"
             "                          in the form address:port:weight\n");
    exit (EXIT_FAILURE);
}

void
glb_cmd_print (FILE* out, glb_cmd_t* cmd)
{
    ulong i;

    fprintf (out, "Incoming address: %s, ",
             glb_socket_addr_to_string (&cmd->inc_addr));
    fprintf (out, "control FIFO: %s\n", cmd->fifo_name);
    fprintf (out, "Control address:  %s\n",
             cmd->ctrl_set ? glb_socket_addr_to_string (&cmd->ctrl_addr) :
             "none");
    fprintf (out, "Number of threads: %lu, source tracking: %s, verbose: %s, "
             "daemon: %s\n",
             cmd->n_threads, cmd->src_tracking ? "ON" : "OFF",
             cmd->verbose ? "ON" : "OFF", cmd->daemonize ? "YES" : "NO");
    fprintf (out, "Destinations: %lu\n", (ulong)cmd->n_dst);

    for (i = 0; i < cmd->n_dst; i++) {
        char tmp[128];
        glb_dst_print (tmp, 128, &cmd->dst[i]);
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

// parses comma separated list of destinations, reallocates and fills conf
static glb_cmd_t*
cmd_parse_dst_list (const char* dst_list[],
                    size_t      n_dst,
                    ulong       default_port)
{
    glb_cmd_t*   ret   = NULL;
    size_t       i;
    const size_t max_dst_len = 256; // addr:port:weight\0
    char         dst_str[max_dst_len + 1] = { 0, };
    ptrdiff_t    dst_len;

    ret = calloc (sizeof(*ret) + n_dst * sizeof(glb_dst_t), 1);
    if (ret) {
        for (i = 0; i < n_dst; i++) {

            dst_len = strlen (dst_list[i]);

            if (dst_len > max_dst_len) {
                fprintf (stderr, "Destination spec too long: %s\n",dst_list[i]);
                free (ret);
                return NULL;
            }

            strncpy (dst_str, dst_list[i], dst_len);

            switch (glb_dst_parse (&ret->dst[i], dst_str, default_port)) {
            case 1:
                // default port is assigned glb_dst_parse()
            case 2:
                // default weight is assigned glb_dst_parse()
            case 3:
                break;
            default: // error parsing destination
                fprintf (stderr, "Invalid destination spec: %s\n", dst_str);
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

glb_cmd_t*
glb_cmd_parse (int argc, char* argv[])
{
    glb_cmd_t    tmp = {{ 0 }}; // initialize to 0
    glb_cmd_t*   ret = NULL;
    const char** dst_list = NULL;
    long         opt = 0;
    int          opt_idx = 0;
    char*        endptr;
    uint16_t     inc_port;

    // Set defaults
    tmp.ctrl_set     = false;
    tmp.fifo_name    = cmd_fifo_name_default;
    tmp.n_threads    = cmd_min_threads;
    tmp.src_tracking = false;
    tmp.verbose      = false;
    tmp.daemonize    = false;

    // parse options
    while ((opt = getopt_long (argc, argv, "c:dfht:svV", cmd_options, &opt_idx))
           != -1) {
        switch (opt) {
        case '?':
        case CMD_OPT_HELP:
            glb_cmd_help(stdout, argv[0]);
            exit (-1);
            break;
        case CMD_OPT_CONTROL:
            if (cmd_parse_addr (&tmp.ctrl_addr, optarg, cmd_ctrl_addr_default))
                return NULL;
            tmp.ctrl_set = true;
            break;
        case CMD_OPT_FIFO:
            tmp.fifo_name = optarg;
            break;
        case CMD_OPT_N_THREADS:
            tmp.n_threads = strtol (optarg, &endptr, 10);
            if ((*endptr != '\0' && *endptr != ' ') || errno) {
                fprintf (stderr, "Bad n_threads value: %s. Integer expected.\n",
                         optarg);
                return NULL;
            }
            if (tmp.n_threads<cmd_min_threads) tmp.n_threads = cmd_min_threads;
            break;
        case CMD_OPT_VERBOSE:
            tmp.verbose = true;
            glb_verbose = true;
            break;
        case CMD_OPT_VERSION:
            printf ("%s v%s\n", PACKAGE, VERSION);
            break;
        case CMD_OPT_DAEMON:
            tmp.daemonize = true;
            break;
        case CMD_OPT_SRC_TRACKING:
            tmp.src_tracking = true;
        default:
            glb_log_warn ("Option '%s'(%d) not supported yet. Ignoring.\n",
                     cmd_options[opt].name, opt);
        }
    }

    // first non-option argument
    if (optind >= argc) {
        fprintf (stderr, "Missing required argument: LISTEN_ADDR.\n");
        return NULL;
    }

    // parse obligatory incoming address
    if (cmd_parse_addr (&tmp.inc_addr, argv[optind], cmd_inc_addr_default)) {
        return NULL;
    }
    inc_port = glb_socket_addr_get_port (&tmp.inc_addr);

    // if number of threads was not specified
    if (!tmp.n_threads) tmp.n_threads = 1;

    // parse destination list
    if (++optind < argc) dst_list = (const char**) &(argv[optind]);
    assert (argc >= optind);
    ret = cmd_parse_dst_list (dst_list, argc - optind, inc_port);

    if (tmp.daemonize) tmp.verbose = false;

    if (ret) {
        ret->inc_addr  = tmp.inc_addr;
        ret->ctrl_addr = tmp.ctrl_addr;
        ret->ctrl_set  = tmp.ctrl_set;
        ret->fifo_name = tmp.fifo_name;
        ret->n_threads = tmp.n_threads;
        ret->verbose   = tmp.verbose;
        ret->daemonize = tmp.daemonize;
        ret->src_tracking = tmp.src_tracking;
    }

    return ret;
}


