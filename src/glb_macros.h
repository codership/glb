/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#ifndef _glb_macros_h_
#define _glb_macros_h_

#if __GNUC__ >= 3
#  define GLB_LIKELY(x)   __builtin_expect((x), 1)
#  define GLB_UNLIKELY(x) __builtin_expect((x), 0)
#else
#  define GLB_LIKELY(x)   (x)
#  define GLB_UNLIKELY(x) (x)
#endif


#endif // _glb_macros_h_
