/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "../config.h" // for version

#include "glb_cnf.h"
#include "glb_limits.h"

glb_cnf_t* glb_cnf = NULL;

#include <errno.h>
#include <stddef.h> // ptrdiff_t
#include <string.h>

static const long long default_check_interval = 1000000000; // 1 sec
static const char      default_fifo_name[]    = "/tmp/glbd.fifo";

glb_cnf_t*
glb_cnf_init ()
{
    glb_cnf_t* ret = (glb_cnf_t*)malloc(sizeof(glb_cnf_t));

    if (ret) // init defaults
    {
        memset (ret, 0, sizeof(*ret));
#ifdef GLBD
        ret->fifo_name = default_fifo_name;
        ret->n_threads = 1;
        ret->max_conn  = glb_get_conn_limit();
        ret->nodelay   = true;
        ret->policy    = GLB_POLICY_LEAST;
#else
        ret->policy    = GLB_POLICY_ROUND;
#endif /* GLBD */
        ret->interval  = default_check_interval;
    }
    else
    {
        fprintf (stderr, "Could not allocate %zu bytes for config struct.\n",
                 sizeof(*ret));
    }

    return ret;
}

// Some constants
#define glb_ip_len_max     256
#define glb_port_max       ((1<<16) - 1)

// parses [addr:]port
int
glb_parse_addr (glb_sockaddr_t* addr,
                const char*     str,
                const char*     default_addr)
{
    const char* port_str;
    ulong       port;
    char*       endptr;
    char        addr_str[glb_ip_len_max + 1] = { 0, };

    port_str = strchr (str, ':');
    if (!port_str) {
        // no separator - only port present
        port_str = str;
        strncpy (addr_str, default_addr, glb_ip_len_max); // any address
    }
    else {
        ptrdiff_t addr_len = port_str - str;
        if (addr_len > glb_ip_len_max) {
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
    if (*endptr != '\0' || port > glb_port_max) {
        fprintf (stderr, "Invalid port spec: %s\n", port_str);
        return -EINVAL;
    }

//    printf ("Option: %s, found addr = '%s', port = '%s'\n",
//            str, addr_str, port_str);
    return glb_sockaddr_init (addr, addr_str, port);
}

// parses array list of destinations
glb_cnf_t*
glb_parse_dst_list (const char* const dst_list[],
                    int         const n_dst,
                    uint16_t    const default_port,
                    glb_cnf_t*  const in)
{
    int    i;
    size_t const new_size = sizeof(*in) + n_dst * sizeof(glb_dst_t);
    glb_cnf_t* out = realloc (in, new_size);

    if (out) {
        for (i = 0; i < n_dst; i++) {
            switch (glb_dst_parse (&out->dst[i], dst_list[i], default_port)) {
            case 1:
                // default port is assigned glb_dst_parse()
            case 2:
                // default weight is assigned glb_dst_parse()
            case 3:
                break;
            default: // error parsing destination
                fprintf (stderr, "Invalid destination spec: %s\n", dst_list[i]);
                free (out);
                return NULL;
            }
        }
        out->n_dst = n_dst;
    }
    else
    {
        fprintf (stderr, "Failed to reallocate conf struct to %zu bytes.\n",
                 new_size);
    }

    return out;
}

#ifdef GLBD

void
glb_print_version (FILE* out)
{
    fprintf (out, "%s v%s (%s)\n", PACKAGE, VERSION,
#if defined(USE_EPOLL)
            "epoll"
#elif defined(USE_POLL)
            "poll"
#else
#error "USE_POLL/USE_EPOLL undefined"
#endif
        );
}

static const char* policy_str[GLB_POLICY_MAX] =
{
    "least connected", "round-robin", "random", "source"
};

void
glb_cnf_print (FILE* out, const glb_cnf_t* cnf)
{
    ulong i;
    glb_sockaddr_str_t inc_addr  = glb_sockaddr_to_str (&cnf->inc_addr);
    glb_sockaddr_str_t ctrl_addr = glb_sockaddr_to_str (&cnf->ctrl_addr);

    glb_print_version(out);
    fprintf (out, "Incoming address: %s, ", inc_addr.str);
    fprintf (out, "control FIFO: %s\n", cnf->fifo_name);
    fprintf (out, "Control  address:  %s\n",
             cnf->ctrl_set ? ctrl_addr.str : "none");
    fprintf (out, "Number of threads: %d, max conn: %d, policy: '%s', "
             "nodelay: %s, defer accept: %s, verbose: %s, daemon: %s\n",
             cnf->n_threads,
             cnf->max_conn,
             policy_str[cnf->policy],
             cnf->nodelay ? "ON" : "OFF",
             cnf->defer_accept ? "ON" : "OFF",
             cnf->verbose ? "ON" : "OFF",
             cnf->daemonize ? "YES" : "NO");
    fprintf (out, "Destinations: %lu\n", (ulong)cnf->n_dst);

    for (i = 0; i < cnf->n_dst; i++) {
        char tmp[256];
        glb_dst_print (tmp, sizeof(tmp), &cnf->dst[i]);
        fprintf (out, "  %2lu: %s\n", i, tmp);
    }
}

#endif /* GLBD */


