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
//static const char env_interval[]= "GLB_INTERVAL";// health check interval
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

static glb_cnf_t*
env_parse_options (glb_cnf_t* const cnf, const char* opts)
{
    glb_cnf_t* ret = cnf;

    if (!opts) return ret;

    char* tmp_opts = strdup (opts);

    if (!tmp_opts) return ret;

    int    argc   = 0;
    char** argv   = NULL;
    char*  endptr = NULL;

    if (glb_parse_token_string (tmp_opts, (const char***)&argv, &argc, '\0'))
        goto cleanup;

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
        case GLB_OPT_LATENCY_COUNT:
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

    if (i < argc) { /* parse bind address */
        glb_sockaddr_t tmp;
        if (!glb_parse_addr (&tmp, argv[i], env_bind_addr_default)) {
            cnf->inc_addr = tmp;
            i++;
        }
    }

    if (i < argc) { /* parse dst list */
        unsigned short const inc_port = glb_sockaddr_get_port (&cnf->inc_addr);
        ret = glb_parse_dst_list ((const char**)(argv + i), argc - i,
                                  inc_port, cnf);
    }

cleanup:

    free (argv);
    free (tmp_opts);

    return ret;
}

static void
env_parse_policy (glb_cnf_t* cnf, const char* p)
{
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

static bool
env_addr_empty (const glb_sockaddr_t* const addr)
{
    glb_sockaddr_t empty;

    memset (&empty, 0, sizeof(empty));

    return glb_sockaddr_is_equal (addr, &empty);
}

glb_cnf_t*
glb_env_parse ()
{
    bool err = false;

    glb_cnf_t* ret = glb_cnf_init(); // initialize to defaults
    if (!ret) return NULL;

    glb_cnf_t* tmp = env_parse_options (ret, getenv (env_options));
    if (!tmp) goto failure;

    ret = tmp;

    const char* const bind_str = getenv (env_bind);
    if (bind_str && strlen(bind_str))
    {
        err = glb_parse_addr (&ret->inc_addr, bind_str, env_bind_addr_default);
    }
    // must make sure that inc_addr is set
    err = err || env_addr_empty (&ret->inc_addr);
    if (err)
    {
        fputs (LIBGLB_PREFIX "Unspecified or invalid \"bind\" address.\n",
               stderr);
        goto failure;
    }

    const char* const targets_tmp = getenv (env_targets);
    char* targets_str = targets_tmp ? strdup(targets_tmp) : NULL;
    if (targets_str && strlen(targets_str))
    {
        const char** dst_list  = NULL;
        int          dst_num   = 0;
        uint16_t     bind_port = glb_sockaddr_get_port (&ret->inc_addr);

        if (!glb_parse_token_string (targets_str, &dst_list, &dst_num, ','))
        {
            assert(dst_list);
            assert(dst_num >= 0);

            tmp = glb_parse_dst_list(dst_list, dst_num, bind_port, ret);

            free (dst_list);

            if (tmp) ret = tmp; else err = true;
        }
        else err = true;
    }
    free (targets_str);

    err = err || (0 == ret->n_dst);

    if (err)
    {
        fputs (LIBGLB_PREFIX "Unspecified or invalid targets list.\n", stderr);
        goto failure;
    }

    env_parse_policy   (ret, getenv (env_policy));
    env_parse_control  (ret, getenv (env_ctrl));
    env_parse_watchdog (ret, getenv (env_watchdog));

    return ret;

failure:

    if (ret->verbose) glb_cnf_print(stderr, ret);

    if (ret->watchdog) free ((void*)ret->watchdog);
    free (ret);
    return NULL;
}
