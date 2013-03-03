/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_env.h"
#include "glb_opt.h"
#include "glb_limits.h"
#include "glb_socket.h"
#include "glb_misc.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>

/* Environment variable names */
static const char env_options[] = "GLB_OPTIONS"; // options string, same as cmd
static const char env_bind[]    = "GLB_BIND"; // address that should be balanced
static const char env_policy[]  = "GLB_POLICY";
static const char env_ctrl[]    = "GLB_CONTROL"; // address to accept control
                                                 // connections
static const char env_targets[] = "GLB_TARGETS"; // balancing targets
static const char env_interval[]= "GLB_INTERVAL";// health check interval
static const char env_watchdog[]= "GLB_WATCHDOG";// watchdog spec string

// Defaults relevant to ENV
static const char env_ctrl_addr_default[] = "127.0.0.1";
static const char env_bind_addr_default[] = "127.0.0.1";

static inline glb_opt_t
env_option_is (const char* opt, const glb_option_t* opts)
{
    if (NULL == opt || '-' != opt[0]) return GLB_OPT_NOOPT;

    /* at this point opt is guaranteed to consist at least of '-' and \0 */

    if ('-' == opt[1]) /* long option */
    {
        while (NULL != opts->name)
        {
            if (!strcmp (opt + 2, opts->name)) return opts->val;
            opts++;
        }
    }
    else              /* short option */
    {
        while (NULL != opts->name)
        {
            if (opt[1] == opts->val) return opts->val;
            opts++;
        }
    }

    return GLB_OPT_NOOPT;
}

static void
env_parse_options (glb_cnf_t* cnf, char* o)
{
    if (!o) return;

    int    argc = 0;
    char** argv = NULL;
    char*  endptr = NULL;

    if (glb_parse_token_string (o, (const char***)&argv, &argc, '\0'))
        return;

    int i;
    for (i = 0; i < argc; i++)
    {
        switch (env_option_is (argv[i], glb_options))
        {
        case GLB_OPT_NOOPT:
            // options ended, now we expect bind addr and dst list
            goto end_opts;
        case GLB_OPT_DISCOVER:
            cnf->discover = true;
            break;
        case GLB_OPT_SINGLE:
            cnf->policy = GLB_POLICY_SINGLE; // implies GLB_OPT_TOP
        case GLB_OPT_TOP:
            cnf->top = true;
            break;
        case GLB_OPT_ROUND_ROBIN:
            cnf->policy = GLB_POLICY_ROUND;
            break;
        case GLB_OPT_CONTROL:
            if (i + 1 < argc) {
                if (!glb_parse_addr (&cnf->ctrl_addr, argv[i+1],
                                     env_ctrl_addr_default)) {
                    i++;
                    cnf->ctrl_set = true;
                }
            }
            break;
        case GLB_OPT_INTERVAL:
            if (i + 1 < argc) {
                long long intvl =
                    glb_time_from_double (strtod (argv[i + 1], &endptr));
                if ((*endptr == '\0' || isspace(*endptr)) && !errno &&
                    intvl > 0) {
                    i++;
                    cnf->interval = intvl;
                }
            }
            break;
        case GLB_OPT_LATENCY:
            if (i + 1 < argc) {
                long lf = strtol (argv[i + 1], &endptr, 10);
                if ((*endptr == '\0' || isspace(*endptr)) && !errno && lf >= 0){
                    i++;
                    cnf->lat_factor = lf;
                }
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
#if 0
        case GLB_OPT_WATCHDOG:
            if (i + 1 < argc) {
                i++;
                cnf->watchdog = argv[i];
            }
            break;
#endif /* 0 */
        case GLB_OPT_EXTRA_POLLS:
            if (i + 1 < argc) {
                glb_time_t ext= glb_time_from_double(strtod(argv[i+1],&endptr));
                if ((*endptr == '\0' || isspace(*endptr)) && !errno && ext >= 0)
                {
                    i++;
                    cnf->extra = ext;
                }
            }
            break;
        default:
            ; // just silently ignore unsupported iptions
        }
    }

end_opts:

    if (i < argc) {
        /* parse bind address */
        glb_sockaddr_t tmp;
        if (!glb_parse_addr (&tmp, argv[i], env_bind_addr_default)) {
            cnf->inc_addr = tmp;
            i++;
        }
    }

    if (i < argc) {
        /* parse dst list */

    }

    free (argv);
}

static void
env_parse_policy (glb_cnf_t* cnf, const char* p)
{
    cnf->policy = GLB_POLICY_ROUND; // default

    if (p)
    {
        if (!strcmp(p, "single"))
        {
            cnf->policy = GLB_POLICY_SINGLE;
            cnf->top    = true;
        }
        else if (p && !strcmp(p, "random")) cnf->policy = GLB_POLICY_RANDOM;
        else if (p && !strcmp(p, "source")) cnf->policy = GLB_POLICY_SOURCE;
    }
}

static void
env_parse_control (glb_cnf_t* cnf, const char* p)
{
    cnf->ctrl_set = false; // default

    if (p)
    {
        cnf->ctrl_set =
            !glb_parse_addr(&cnf->ctrl_addr, p, env_ctrl_addr_default);
    }
}

static void
env_parse_watchdog (glb_cnf_t* cnf, const char* p)
{
    if (p) cnf->watchdog = strdup(p);
}

glb_cnf_t*
glb_env_parse ()
{
    bool err;

    if (!getenv (env_bind))    return NULL;
    if (!getenv (env_targets)) return NULL;

    glb_cnf_t* ret = glb_cnf_init(); // initialize to defaults
    if (!ret) return NULL;

    err = glb_parse_addr (&ret->inc_addr, getenv (env_bind),
                          env_bind_addr_default);
    if (err) goto failure;

    const char** dst_list = NULL;
    int          dst_num  = 0;
    uint16_t     bind_port = glb_sockaddr_get_port (&ret->inc_addr);

    if (!glb_parse_token_string (getenv(env_targets), &dst_list, &dst_num, ','))
    {
        assert(dst_list);
        assert(dst_num >= 0);

        glb_cnf_t* tmp = glb_parse_dst_list(dst_list, dst_num, bind_port, ret);
        if (tmp)
        {
            ret = tmp;

            env_parse_options  (ret, getenv (env_options));
            env_parse_policy   (ret, getenv (env_policy));
            env_parse_control  (ret, getenv (env_ctrl));
            env_parse_watchdog (ret, getenv (env_watchdog));
        }
        else err = true;

        free (dst_list);
    }
    else err = true;

    if (!err) return ret;

failure:

    free (ret);
    return NULL;
}


