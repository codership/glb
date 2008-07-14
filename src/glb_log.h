/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_log_h_
#define _glb_log_h_

#include <stdlib.h>

typedef enum glb_log_level
{
    GLB_LOG_FATAL,
    GLB_LOG_ERROR,
    GLB_LOG_WARNING,
    GLB_LOG_INFO,
    GLB_LOG_DEBUG
} glb_log_level_t;

extern void
glb_log (glb_log_level_t level,
         const char*     file,
         const char*     function,
         int             line,
         char*           format, ...);


typedef enum glb_log_type
{
    GLB_LOG_SYSLOG,
    GLB_LOG_PRINTF
} glb_log_type_t;

extern long
glb_log_init (glb_log_type_t log_type);

#define glb_log_fatal(format, ...) \
        glb_log (GLB_LOG_FATAL, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
                 format, ## __VA_ARGS__, NULL)

#define glb_log_error(format, ...) \
        glb_log (GLB_LOG_ERROR, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
                 format, ## __VA_ARGS__, NULL)

#define glb_log_warn(format, ...) \
        glb_log (GLB_LOG_WARNING, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
                 format, ## __VA_ARGS__, NULL)

#define glb_log_info(format, ...) \
        glb_log (GLB_LOG_INFO, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
                 format, ## __VA_ARGS__, NULL)

#define glb_log_debug(format, ...) \
        glb_log (GLB_LOG_DEBUG, __FILE__, __PRETTY_FUNCTION__, __LINE__,\
                 format, ## __VA_ARGS__, NULL)


#endif // _glb_log_h_
