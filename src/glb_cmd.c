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

typedef enum conf_opt
{
    CONF_OPT_CONTROL      = 'c',
    CONF_OPT_HELP         = 'h',
    CONF_OPT_SRC_TRACKING = 's',
    CONF_OPT_N_THREADS    = 't',
    CONF_OPT_VERBOSE      = 'v',
    CONF_OPT_VERSION      = 'V'
} conf_opt_t;

static option_t conf_options[] =
{
    { "control",         RA, NULL, CONF_OPT_CONTROL       },
    { "help",            NA, NULL, CONF_OPT_HELP          },
    { "src_tracking",    NA, NULL, CONF_OPT_SRC_TRACKING  },
    { "source_tracking", NA, NULL, CONF_OPT_SRC_TRACKING  },
    { "threads",         RA, NULL, CONF_OPT_N_THREADS     },
    { "verbose",         NA, NULL, CONF_OPT_VERBOSE       },
    { "version",         NA, NULL, CONF_OPT_VERSION       },
    { 0, 0, 0, 0 }
};

// Some constants
static const int conf_dst_separator    = ':';
static const int conf_list_separator   = ',';
static const ptrdiff_t conf_ip_len_max = 16;
static const ptrdiff_t conf_ip_len_min = 7;
static const ulong conf_port_max       = (1 << 16) - 1;

void
glb_conf_help (FILE* out, const char* progname)
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
glb_conf_print (FILE* out, glb_conf_t* conf)
{
    ulong i;

    fprintf (out, "Incoming address: %s:%lu, ",
             inet_ntoa(conf->inc_addr), conf->inc_port);
    fprintf (out, "control address: %s:%lu\n",
             inet_ntoa(conf->ctrl_addr), conf->ctrl_port);
    fprintf (out, "Number of threads: %lu, source tracking: %s, verbose: %s\n",
             conf->n_threads, conf->src_tracking ? "ON" : "OFF",
             conf->verbose ? "ON" : "OFF");
    fprintf (out, "Destinations: %lu\n", (ulong)conf->n_dst);

    for (i = 0; i < conf->n_dst; i++) {
        fprintf (out, "  %2lu: %s:%lu,\tweight: %lu\n",
                 i, inet_ntoa(conf->dst[i].addr),
                 conf->dst[i].port, conf->dst[i].weight);
    }
}

// parses [addr:]port
static long
conf_parse_addr (struct in_addr* addr,
                 ulong*          port,
                 const char*     str)
{
    const char* port_str;
    char*       endptr;
    char        addr_str[conf_ip_len_max + 1] = { 0, };

    port_str = strchr (str, conf_dst_separator);
    if (!port_str) {
        // no separator - only port present
        port_str = str;
    }
    else {
        ptrdiff_t addr_len = port_str - str;
        if (addr_len > conf_ip_len_max) {
            fprintf (stderr, "Invalid IP address: %s\n", str);
            return -EINVAL;
        }
        port_str = port_str + 1;
        strncpy (addr_str, str, addr_len);
        if (!inet_aton (addr_str, addr)) {
            fprintf (stderr, "Invalid IP address: %s\n", addr_str);
            return -EINVAL;
        }
    }

    *port = strtoul (port_str, &endptr, 10);
    if (*endptr != '\0' || *port > conf_port_max) {
        fprintf (stderr, "Invalid port: %s\n", port_str);
        return -EINVAL;
    }

//    printf ("Option: %s, found addr = '%s', port = '%s'\n",
//            str, addr_str, port_str);
    return 0;
}

// parses addr:port:weight string, stores in dst
// returns number of parsed fields or negative error code
static long
conf_parse_dst (glb_conf_dst_t* dst, const char* s, const char** next)
{
    const char* token;
    char*       endptr;
    char        addr_str[conf_ip_len_max + 1] = { 0, };
    ptrdiff_t   addr_len;
    long        ret;

    // parse IP address
    *next  = strchr (s, conf_list_separator);
    endptr = strchr (s, conf_dst_separator);
    if (endptr && *next && endptr >= *next) endptr = NULL;

    if (NULL != endptr)
        addr_len = endptr - s;
    else if (NULL != *next)
        addr_len = *next - s;
    else
        addr_len = strlen (s);

    // make sure that whenever we return, *next has the rigth value
    if (NULL != *next) *next = *next + 1; // skip list separator

    if (addr_len > conf_ip_len_max || addr_len < conf_ip_len_min)
        return -EINVAL;

    strncpy (addr_str, s, addr_len);
    ret = inet_aton (addr_str, &dst->addr);
    if (!ret)
        return -EINVAL;
    else if (NULL == endptr) // string or item is over
        return 1;

    // parse port
    assert (*endptr == conf_dst_separator);
    token = endptr + 1;
    dst->port = strtoul (token, &endptr, 10);
    if (*endptr != conf_dst_separator  &&
        *endptr != conf_list_separator &&
        *endptr != '\0') {
        // port field doesn't consist only of numbers
        return -EINVAL;
    }
    if (dst->port > conf_port_max) // value of 0 means no setting, don't check
        return -EINVAL;
    else if (*endptr == '\0' || *endptr == conf_list_separator) // string is over
        return 2;

    // parse weight
    assert (*endptr == conf_dst_separator);
    token = endptr + 1;
    dst->weight = strtoul (token, &endptr, 10);
    if (*endptr != conf_list_separator &&
        *endptr != '\0') {
        // port field doesn't consist only of numbers
        return -EINVAL;
    }
    return 3;
}

// parses comma separated list of destinations, reallocates and fills conf
static glb_conf_t*
conf_parse_dst_list (const char* dst_list,
                     ulong       default_port,
                     long        default_weight)
{
    glb_conf_t* ret  = NULL;
    const char* next = dst_list;
    size_t n_dst = 0, i;

    // find out how many destinations
    while (next) {
        n_dst++;
        next = strchr (next, conf_list_separator);
        if (next) next++; // skip separator
    }

    ret = calloc (sizeof(*ret) + n_dst * sizeof(glb_conf_dst_t), 1);
    if (ret) {
        next = dst_list;
        for (i = 0; i < n_dst; i++) {
            assert (next);
            switch (conf_parse_dst (&ret->dst[i], next, &next)) {
            case 1:
                ret->dst[i].port   = default_port;
            case 2:
                ret->dst[i].weight = default_weight;
            case 3:
                break;
            default: // error parsing destination
                fprintf (stderr, "Invalid destination spec: %s\n", dst_list);
                free (ret);
            }
        }
        ret->n_dst = n_dst;
    }
    return ret;
}

// General defaults
static const char* conf_inc_addr_default     = "0.0.0.0";
static const char* conf_ctrl_addr_default    = "127.0.0.1";
static const ulong conf_min_threads          = 1;
static const bool  conf_src_tracking_default = false;
static const bool  conf_verbose_default      = false;

glb_conf_t*
glb_conf_cmd_parse (int argc, char* argv[])
{
    glb_conf_t  tmp = {{ 0 }}; // initialize to 0
    glb_conf_t* ret = NULL;
    const char* dst_list = NULL;
    long        opt = 0;
    int         opt_idx = 0;
    char*       endptr;

    // Set defaults
    if (!inet_aton (conf_inc_addr_default,  &tmp.inc_addr))  abort();
    if (!inet_aton (conf_ctrl_addr_default, &tmp.ctrl_addr)) abort();
    tmp.n_threads    = conf_min_threads;
    tmp.src_tracking = conf_src_tracking_default;
    tmp.verbose      = conf_verbose_default;

    // parse options
    while ((opt = getopt_long (argc, argv, "c:ht:svV", conf_options, &opt_idx)) != -1) {
        switch (opt) {
        case '?':
        case CONF_OPT_HELP:
            glb_conf_help(stdout, argv[0]);
            break;
        case CONF_OPT_CONTROL:
            if (conf_parse_addr (&tmp.ctrl_addr, &tmp.ctrl_port, optarg)) {
                return NULL;
            }
            break;
        case CONF_OPT_N_THREADS:
            tmp.n_threads = strtoul (optarg, &endptr, 10);
            if ((*endptr != '\0' && *endptr != ' ') || errno) {
                fprintf (stderr, "Bad n_threads value: %s. Integer expected.\n",
                         optarg);
                return NULL;
            }
            break;
        case CONF_OPT_SRC_TRACKING:
            tmp.src_tracking = true;
            break;
        case CONF_OPT_VERBOSE:
            tmp.verbose = true;
            break;
        case CONF_OPT_VERSION:
        default:
            fprintf (stderr, "Option '%s' not supported yet. Ignoring.\n",
                     conf_options[opt].name);
        }
    }

    // first non-option argument
    if (optind >= argc) {
        fprintf (stderr, "Missing required argument: LISTEN_ADDR.\n");
        return NULL;
    }

    // parse obligatory incoming address
    if (conf_parse_addr (&tmp.inc_addr, &tmp.inc_port, argv[optind])) {
        return NULL;
    }

    // if control port was not specified
    if (!tmp.ctrl_port) tmp.ctrl_port = tmp.inc_port + 1;
    // if number of threads was not specified
    if (!tmp.n_threads) tmp.n_threads = 1;

    // parse destination list
    if (++optind < argc) dst_list = argv[optind];
    ret = conf_parse_dst_list (dst_list, tmp.inc_port, 100);

    if (ret) {
        ret->inc_addr  = tmp.inc_addr;
        ret->inc_port  = tmp.inc_port;
        ret->ctrl_addr = tmp.ctrl_addr;
        ret->ctrl_port = tmp.ctrl_port;
        ret->n_threads = tmp.n_threads;
        ret->verbose   = tmp.verbose;
        ret->src_tracking = tmp.src_tracking;
    }

    return ret;
}


