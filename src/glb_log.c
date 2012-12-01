/*
 * Copyright (C) 2008-2012 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "glb_log.h"

static glb_log_type_t log_type = GLB_LOG_PRINTF;

void
glb_log (glb_log_level_t level,
         const char*     file,
         const char*     function,
         int             line,
         char*           format, ...)
{
    va_list ap;
    size_t  buf_len = BUFSIZ;
    char    buf[buf_len];
    ssize_t len = 0;

    // just omit function name for now
    len = snprintf (buf, buf_len, "%s:%d: ", file, line);
    if (len < 0) abort();

    if (buf_len > (size_t)len) {
        va_start (ap, format); 
        vsnprintf (buf + len, buf_len - len, format, ap);
        va_end (ap);
    }
    buf[buf_len - 1] = '\0';

    switch (log_type)
    {
    case GLB_LOG_SYSLOG:
    {
        int facility = LOG_DAEMON;
        int priority;

        switch (level)
        {
        case GLB_LOG_FATAL:
            priority = LOG_MAKEPRI(facility, LOG_CRIT); break;
        case GLB_LOG_ERROR:
            priority = LOG_MAKEPRI(facility, LOG_ERR); break;
        case GLB_LOG_WARNING:
            priority = LOG_MAKEPRI(facility, LOG_WARNING); break;
        case GLB_LOG_INFO:
            priority = LOG_MAKEPRI(facility, LOG_INFO); break;
        case GLB_LOG_DEBUG:
        default:
            priority = LOG_MAKEPRI(facility, LOG_DEBUG); break;
        }

        syslog (priority, "%s", buf);
        return;
    }
    case GLB_LOG_PRINTF:
    {
        FILE* out = stdout;
        const char* lvl;

        switch (level)
        {
        case GLB_LOG_FATAL:   lvl = "  FATAL: "; out = stderr; break;
        case GLB_LOG_ERROR:   lvl = "  ERROR: "; out = stderr; break;
        case GLB_LOG_WARNING: lvl = "WARNING: "; out = stderr; break;
        case GLB_LOG_INFO:    lvl = "   INFO: "; break;
        case GLB_LOG_DEBUG:   lvl = "  DEBUG: "; break;
        default:              lvl = "UNKNOWN: "; break;
        }

        fprintf (out, "%s%s\n", lvl, buf);
        return;
    }
    default:
        abort();
    }
}

long
glb_log_init (glb_log_type_t lt)
{
    switch (lt) {
    case GLB_LOG_SYSLOG:
        setlogmask (LOG_UPTO (LOG_DEBUG)); // avoid debug messages in syslog
        openlog (NULL, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_DAEMON);
    case GLB_LOG_PRINTF:
        log_type = lt;
        return 0;
    default:
        fprintf (stderr, "Bad logger type: %d.\n", lt);
        return -1;
    }
}
