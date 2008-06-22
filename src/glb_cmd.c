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

#include "glb_cmd.h"
#include "glb_socket.h"

extern bool glb_verbose;

typedef enum cmd_opt
{
    CMD_OPT_CONTROL      = 'c',
    CMD_OPT_HELP         = 'h',
    CMD_OPT_SRC_TRACKING = 's',
    CMD_OPT_N_THREADS    = 't',
    CMD_OPT_VERBOSE      = 'v',
    CMD_OPT_VERSION      = 'V'
} cmd_opt_t;

static option_t cmd_options[] =
{
    { "control",         RA, NULL, CMD_OPT_CONTROL       },
    { "help",            NA, NULL, CMD_OPT_HELP          },
    { "src_tracking",    NA, NULL, CMD_OPT_SRC_TRACKING  },
    { "source_tracking", NA, NULL, CMD_OPT_SRC_TRACKING  },
    { "threads",         RA, NULL, CMD_OPT_N_THREADS     },
    { "verbose",         NA, NULL, CMD_OPT_VERBOSE       },
    { "version",         NA, NULL, CMD_OPT_VERSION       },
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
             "  --control [HOST:]PORT   "
             "listen for control connections on this address\n"
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
             "  [H1[:P1[:W1]]][,H2[:P2[:W2]]]... "
             " - a comma separated list of destinations\n"
             "                          in the form address:port:weight\n");
    exit (EXIT_FAILURE);
}

void
glb_cmd_print (FILE* out, glb_cmd_t* cmd)
{
    ulong i;

    fprintf (out, "Incoming address: %s, ",
             glb_socket_addr_to_string (&cmd->inc_addr));
    fprintf (out, "control address: %s\n",
             glb_socket_addr_to_string (&cmd->ctrl_addr));
    fprintf (out, "Number of threads: %lu, source tracking: %s, verbose: %s\n",
             cmd->n_threads, cmd->src_tracking ? "ON" : "OFF",
             cmd->verbose ? "ON" : "OFF");
    fprintf (out, "Destinations: %lu\n", (ulong)cmd->n_dst);

    for (i = 0; i < cmd->n_dst; i++) {
        fprintf (out, "  %2lu: ", i);
        glb_dst_print (out, &cmd->dst[i]);
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
cmd_parse_dst_list (const char* dst_list,
                    ulong       default_port)
{
    glb_cmd_t*   ret   = NULL;
    const char*  next  = dst_list;
    size_t       n_dst = 0, i;
    const size_t max_dst_len = 256; // addr:port:weight\0
    char         dst_str[max_dst_len + 1] = { 0, };
    ptrdiff_t    dst_len;

    // find out how many destinations
    while (next) {
        n_dst++;
        next = strchr (next, cmd_list_separator);
        if (next) next++; // skip separator
    }

    ret = calloc (sizeof(*ret) + n_dst * sizeof(glb_dst_t), 1);
    if (ret) {
        for (i = 0; i < n_dst; i++) {

            if ((next = strchr (dst_list, cmd_list_separator))) {
                dst_len = next - dst_list;
            }
            else {
                dst_len = strlen (dst_list);
            }

            if (dst_len > max_dst_len) {
                fprintf (stderr, "Destination spec too long: %s\n", dst_list);
                free (ret);
                return NULL;
            }

            strncpy (dst_str, dst_list, dst_len);
            dst_list = next + 1;

            switch (glb_dst_parse (&ret->dst[i], dst_str)) {
            case 1:
                glb_dst_set_port (&ret->dst[i], default_port);
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
static const char* cmd_inc_addr_default     = "0.0.0.0";
static const char* cmd_ctrl_addr_default    = "127.0.0.1";
static const ulong cmd_min_threads          = 1;
static const bool  cmd_src_tracking_default = false;
static const bool  cmd_verbose_default      = false;

glb_cmd_t*
glb_cmd_parse (int argc, char* argv[])
{
    glb_cmd_t   tmp = {{ 0 }}; // initialize to 0
    glb_cmd_t*  ret = NULL;
    const char* dst_list = NULL;
    long        opt = 0;
    int         opt_idx = 0;
    char*       endptr;
    uint16_t    inc_port;

    // Set defaults
//    if (!inet_aton (cmd_inc_addr_default,  &tmp.inc_addr))  abort();
//    if (!inet_aton (cmd_ctrl_addr_default, &tmp.ctrl_addr)) abort();
    tmp.n_threads    = cmd_min_threads;
    tmp.src_tracking = cmd_src_tracking_default;
    tmp.verbose      = cmd_verbose_default;

    // parse options
    while ((opt = getopt_long (argc, argv, "c:ht:svV", cmd_options, &opt_idx))
           != -1) {
        switch (opt) {
        case '?':
        case CMD_OPT_HELP:
            glb_cmd_help(stdout, argv[0]);
            break;
        case CMD_OPT_CONTROL:
            if (cmd_parse_addr (&tmp.ctrl_addr, optarg, cmd_ctrl_addr_default))
                return NULL;
            break;
        case CMD_OPT_N_THREADS:
            tmp.n_threads = strtoul (optarg, &endptr, 10);
            if ((*endptr != '\0' && *endptr != ' ') || errno) {
                fprintf (stderr, "Bad n_threads value: %s. Integer expected.\n",
                         optarg);
                return NULL;
            }
            break;
        case CMD_OPT_SRC_TRACKING:
            tmp.src_tracking = true;
            break;
        case CMD_OPT_VERBOSE:
            tmp.verbose = true;
            glb_verbose = true;
            break;
        case CMD_OPT_VERSION:
        default:
            fprintf (stderr, "Option '%s' not supported yet. Ignoring.\n",
                     cmd_options[opt].name);
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

    // if control address was not specified
    if (!glb_socket_addr_get_port (&tmp.ctrl_addr)) {
        glb_socket_addr_init (&tmp.ctrl_addr, cmd_ctrl_addr_default,
                              inc_port + 1);
    }
    // if number of threads was not specified
    if (!tmp.n_threads) tmp.n_threads = 1;

    // parse destination list
    if (++optind < argc) dst_list = argv[optind];
    ret = cmd_parse_dst_list (dst_list, inc_port);

    if (ret) {
        ret->inc_addr  = tmp.inc_addr;
        ret->ctrl_addr = tmp.ctrl_addr;
        ret->n_threads = tmp.n_threads;
        ret->verbose   = tmp.verbose;
        ret->src_tracking = tmp.src_tracking;
    }

    return ret;
}


