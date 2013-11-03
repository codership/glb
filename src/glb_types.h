/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * For the moment this defines ulong for FreeBSD and other abominations
 *
 * $Id: glb_types.h 160 2013-11-03 14:49:02Z alex $
 */

#ifndef _glb_types_h_
#define _glb_types_h_

#include <sys/types.h>

#ifndef ulong
typedef u_long ulong;
#endif // ulong

#endif // _glb_types_h_
