/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * Read configuration parameters from command line.
 *
 * $Id$
 */

#ifndef _glb_opt_h_
#define _glb_opt_h_

#include <getopt.h>

typedef struct option glb_option_t;

#define GLB_NA no_argument
#define GLB_RA required_argument
#define GLB_OA optional_argument

typedef enum glb_opt
{
    GLB_OPT_NOOPT        = 0,
    GLB_OPT_DISCOVER     = 'D',
    GLB_OPT_KEEPALIVE    = 'K',
    GLB_OPT_LATENCY_COUNT= 'L',
    GLB_OPT_SINGLE       = 'S',
    GLB_OPT_TOP          = 'T',
    GLB_OPT_VERSION      = 'V',
    GLB_OPT_SYNCHRONOUS  = 'Y',
    GLB_OPT_DEFER_ACCEPT = 'a',
    GLB_OPT_ROUND_ROBIN  = 'b',
    GLB_OPT_CONTROL      = 'c',
    GLB_OPT_DAEMON       = 'd',
    GLB_OPT_FIFO         = 'f',
    GLB_OPT_HELP         = 'h',
    GLB_OPT_INTERVAL     = 'i',
    GLB_OPT_LINGER       = 'l',
    GLB_OPT_MAX_CONN     = 'm',
    GLB_OPT_NODELAY      = 'n',
    GLB_OPT_RANDOM       = 'r',
    GLB_OPT_SRC_TRACKING = 's',
    GLB_OPT_N_THREADS    = 't',
    GLB_OPT_VERBOSE      = 'v',
    GLB_OPT_WATCHDOG     = 'w',
    GLB_OPT_EXTRA_POLLS  = 'x'
} glb_opt_t;

static glb_option_t glb_options[] =
{
    { "discover",        GLB_NA, NULL, GLB_OPT_DISCOVER      },
    { "keepalive",       GLB_NA, NULL, GLB_OPT_KEEPALIVE     },
    { "latency",         GLB_RA, NULL, GLB_OPT_LATENCY_COUNT },
    { "single",          GLB_NA, NULL, GLB_OPT_SINGLE        },
    { "top",             GLB_NA, NULL, GLB_OPT_TOP           },
    { "version",         GLB_NA, NULL, GLB_OPT_VERSION       },
    { "defer-accept",    GLB_NA, NULL, GLB_OPT_DEFER_ACCEPT  },
    { "round",           GLB_NA, NULL, GLB_OPT_ROUND_ROBIN   },
    { "round-robin",     GLB_NA, NULL, GLB_OPT_ROUND_ROBIN   },
    { "rrb",             GLB_NA, NULL, GLB_OPT_ROUND_ROBIN   },
    { "control",         GLB_RA, NULL, GLB_OPT_CONTROL       },
    { "daemon",          GLB_NA, NULL, GLB_OPT_DAEMON        },
    { "fifo",            GLB_RA, NULL, GLB_OPT_FIFO          },
    { "help",            GLB_NA, NULL, GLB_OPT_HELP          },
    { "interval",        GLB_RA, NULL, GLB_OPT_INTERVAL      },
    { "linger",          GLB_NA, NULL, GLB_OPT_LINGER        },
    { "max_conn",        GLB_RA, NULL, GLB_OPT_MAX_CONN      },
    { "connections",     GLB_RA, NULL, GLB_OPT_MAX_CONN      },
    { "nodelay",         GLB_NA, NULL, GLB_OPT_NODELAY       },
    { "random",          GLB_NA, NULL, GLB_OPT_RANDOM        },
    { "source",          GLB_NA, NULL, GLB_OPT_SRC_TRACKING  },
    { "src_tracking",    GLB_NA, NULL, GLB_OPT_SRC_TRACKING  },
    { "source_tracking", GLB_NA, NULL, GLB_OPT_SRC_TRACKING  },
    { "threads",         GLB_RA, NULL, GLB_OPT_N_THREADS     },
    { "verbose",         GLB_NA, NULL, GLB_OPT_VERBOSE       },
    { "watchdog",        GLB_RA, NULL, GLB_OPT_WATCHDOG      },
    { "extra",           GLB_RA, NULL, GLB_OPT_EXTRA_POLLS   },
    { 0, 0, 0, 0 }
};

#endif // _glb_opt_h_
