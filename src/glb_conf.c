/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

//#include <unistd.h>
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

#include "glb_conf.h"

typedef enum conf_opt
{
    CONF_OPT_HELP,
    CONF_OPT_INCOMING_IP,
    CONF_OPT_INCOMING_PORT,
    CONF_OPT_CONTROL,
    CONF_OPT_N_THREADS,
    CONF_OPT_SRC_TRACKING,
    CONF_OPT_DESTINATIONS,
    CONF_OPT_VERBOSE,
    CONF_OPT_VERSION
} conf_opt_t;

static option_t conf_options[] =
{
    { "help",            NA, NULL, CONF_OPT_HELP          },
    { "bind",            RA, NULL, CONF_OPT_INCOMING_IP   },
    { "port",            RA, NULL, CONF_OPT_INCOMING_PORT },
    { "control",         RA, NULL, CONF_OPT_CONTROL       },
    { "threads",         RA, NULL, CONF_OPT_N_THREADS     },
    { "src_tracking",    NA, NULL, CONF_OPT_SRC_TRACKING  },
    { "source_tracking", NA, NULL, CONF_OPT_SRC_TRACKING  },
    { "dst",             RA, NULL, CONF_OPT_DESTINATIONS  },
    { "dest",            RA, NULL, CONF_OPT_DESTINATIONS  },
    { "destinations",    RA, NULL, CONF_OPT_DESTINATIONS  },
    { "verbose",         NA, NULL, CONF_OPT_VERSION       },
    { "version",         NA, NULL, CONF_OPT_DESTINATIONS  },
    { 0, 0, 0, 0 }
};

// Some constants
static const int conf_dst_separator  = ':';
static const int conf_list_separator = ',';
static const ptrdiff_t conf_ip_len_max  = 16;
static const ptrdiff_t conf_ip_len_min  = 7;
static const ulong conf_port_max = (1 << 16) - 1;

void
glb_conf_help (const char* progname)
{
    FILE* out = stdout;

    fprintf (out, "Usage:\n  %s [OPTIONS] <INC_PORT> "
             "[H1[:P1[:W1]]][,H2[:P2[:W2]]]...\nOPTIONS:\n", progname);
    fprintf (out, "  --help                  "
             "this help message\n");
    fprintf (out, "  --bind <INC_HOST>       "
             "bint listener to this IP address\n");
    fprintf (out, "  --control <HOST[:PORT]> "
             "listen for control connections on this address\n");
    fprintf (out, "                          "
             "by default PORT = INC_PORT + 1\n");
    fprintf (out, "  --threads N             "
             "number of working threads (number of CPU cores)\n");
    fprintf (out, "  --source_tracking       "
             "turn on source tracking: route connections from one source\n"
             "to the same destination\n");
    fprintf (out, "  --verbose               "
             "turn on verbose reporting\n");
    fprintf (out, "  --version               "
             "print program version\n");
    fprintf (out, "  <INC_PORT>              "
             "port to listen for incoming TCP connections\n");
    fprintf (out, "  [H1[:P1[:W1]]][,H2[:P2[:W2]]]...\n"
             "                         a comma separated list of destinations\n"
             "                         in the form address:port:weight\n");
    exit (EXIT_FAILURE);
}

void
glb_conf_print (FILE* out, glb_conf_t* conf)
{
    ulong i;

    fprintf (out, "Incoming address: %s:%lu, control address: %s:%lu\n",
             inet_ntoa(conf->inc_addr), conf->inc_port,
             inet_ntoa(conf->ctrl_addr), conf->ctrl_port);
    fprintf (out, "Number of threads: %lu, source tracking: %s, verbose: %s\n",
             conf->n_threads, conf->src_tracking ? "ON" : "OFF",
             conf->verbose ? "ON" : "OFF");
    fprintf (out, "Destinations: %lu\n", conf->n_dst);

    for (i = 0; i < conf->n_dst; i++) {
        fprintf (out, "  %2lu: %s:%lu,\twight: %lu\n",
                 i, inet_ntoa(conf->dst[i].addr),
                 conf->dst[i].port, conf->dst[i].weight);
    }
}

// parses addr:port:weight string, stores in dst
// returns number of parse fields or negative error code
static long
conf_parse_dst (glb_conf_dst_t* dst, const char* s)
{
    const char* token;
    char* endptr;
    char addr_str[conf_ip_len_max + 1] = { 0, };
    size_t addr_len;
    long ret;

    // parse IP address
    endptr = strchr (s, conf_dst_separator);
    if (NULL != endptr)
        addr_len = endptr - s;
    else
        addr_len = strlen (s);
    if (addr_len > conf_ip_len_max || addr_len < conf_ip_len_min)
        return -EINVAL;

    strncpy (addr_str, s, addr_len);
    ret = inet_aton (addr_str, &dst->addr);
    if (!ret)
        return -EINVAL;
    else if (NULL == endptr) // string is over
        return 1;

    // parse port
    assert (*endptr == conf_dst_separator);
    token = endptr + 1;
    dst->port = strtoul (token, &endptr, 10);
    if (*endptr != conf_dst_separator || *endptr != '\0') {
        // port field is doesn't consist only of numbers
        return -EINVAL;
    }
    if (dst->port > conf_port_max) // value of 0 means no setting, don't check
        return -EINVAL;
    else if (*endptr == '\0') // string is over
        return 2;

    // parse weight
    assert (*endptr == conf_dst_separator);
    token = endptr + 1;
    dst->weight = strtoul (token, &endptr, 10);
    if (*endptr != conf_list_separator || *endptr != '\0') {
        // port field is doesn't consist only of numbers
        return -EINVAL;
    }
    return 3;
}

// parses commaseparated list of destinations, reallocates and fills conf
static glb_conf_t*
conf_parse_dst_list (const char* dst_list, size_t len)
{
    glb_conf_t* ret = NULL;
    return ret;
}

// Defaults
static const char* conf_inc_addr_default     = "0.0.0.0";
static const char* conf_ctrl_addr_default    = "127.0.0.1";
static const ulong conf_min_threads          = 1;
static const bool  conf_src_tracking_default = false;
static const bool  conf_verbose_default      = false;

glb_conf_t*
glb_conf_cmd_parse (int argc, char* argv[])
{
    glb_conf_t tmp = {{ 0 }}; // initialize to 0
    glb_conf_t* ret = NULL;
    long opt = 0;
    int opt_idx = 0;

    // Set defaults
    if (!inet_aton (conf_inc_addr_default,  &tmp.inc_addr))  abort();
    if (!inet_aton (conf_ctrl_addr_default, &tmp.ctrl_addr)) abort();
    tmp.n_threads    = conf_min_threads;
    tmp.src_tracking = conf_src_tracking_default;
    tmp.verbose      = conf_verbose_default;

    // parse options
    while ((opt = getopt_long (argc, argv, "", conf_options, &opt_idx)) != -1) {
        switch (opt) {
        case '?':
        case CONF_OPT_HELP:
            glb_conf_help(argv[0]);
            break;
        default:
            fprintf (stderr, "Option '%s' not supported yet. Ignoring.\n",
                     conf_options[opt].name);
        }
    }

    opt_idx++; // first non-option argument
    if (opt_idx >= argc) {
        fprintf (stderr, "Missing required argument - incoming port.\n");
        glb_conf_help(argv[0]);
    }
    else {
        char* endptr;
        tmp.inc_port = strtoul (argv[opt_idx], &endptr, 10);
    }

    printf ("opt_idx = %d\n", opt_idx);
    return ret;
}


