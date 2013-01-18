/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_env.h"
#include "glb_limits.h"
#include "glb_socket.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
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

/*!
 * convert string into array of tokens
 *
 * @param sep - additional separator to whitespace
 */
static bool
env_parse_token_string (char*         tok_str,
                        const char*** tok_list,
                        int*          tok_num,
                        int           sep)
{
    assert (tok_str);

    *tok_list = NULL;
    *tok_num  = 0;

    if (!tok_str) return true;

    size_t const tlen = strlen(tok_str);
    if (!tlen) return true;

    const char** list = NULL;
    int num = 0;

    size_t i;
    for (i = 1; i <= tlen; i++) /* we can skip the first string char */
    {
        if (isspace(tok_str[i]) || sep == tok_str[i]) tok_str[i] = '\0';
        if (tok_str[i] == '\0' && tok_str[i-1] != '\0') num++;/* end of token */
    }

    list = calloc (num, sizeof(const char*));
    if (!list) return true;

    list[0] = tok_str;
    num = 1;

    for (i = 1; i <= tlen; i++)
    {
        if (tok_str[i-1] == '\0' && tok_str[i] != '\0') /* beginning of token */
        {
            list[num] = &tok_str[i];
            num++;
        }
    }

    *tok_list = list;
    *tok_num  = num;

    return false;
}

static inline bool
env_option_is (const char* opt, const char* shopt, const char* lopt)
{
    return false; //stub until 1.0
}

static void
env_parse_options (glb_cnf_t* cnf, char* o)
{
    if (!o) return;

    int    argc = 0;
    char** argv = NULL;

    if (env_parse_token_string (o, (const char***)&argv, &argc, '\0'))
        return;

    int i;
    for (i = 0; i < argc; i++)
    {
        // stub
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
env_parse_interval (glb_cnf_t* cnf, const char* p)
{
    if (!p) return;

    char*  endptr;
    glb_time_t tmp = glb_time_from_double (strtod (p, &endptr));

    if ((*endptr != '\0' && !isspace(*endptr)) || errno || tmp <= 0) return;

    cnf->interval = tmp;
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

    if (!env_parse_token_string (getenv(env_targets), &dst_list, &dst_num, ','))
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
            env_parse_interval (ret, getenv (env_interval));
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


