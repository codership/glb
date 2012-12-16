# Copyright (C) 2008-2012 Codership Oy <info@codership.com>
# $Id$

COMMON_SRCS = \
	glb_socket.c   \
	glb_dst.c      \
	glb_cnf.c      \
	glb_router.c   \
	glb_control.c  \
	glb_wdog.c     \
	glb_wdog_backend.c

sbin_PROGRAMS = glbd

glbd_SOURCES = \
	$(COMMON_SRCS) \
	glb_log.c      \
	glb_signal.c   \
	glb_daemon.c   \
	glb_cmd.c      \
	glb_pool.c     \
	glb_listener.c \
	glb_limits.c   \
	glb_main.c

glbd_CFLAGS = $(AM_CFLAGS) -DGLBD

lib_LTLIBRARIES   = libglb.la

libglb_la_SOURCES = \
	$(COMMON_SRCS) \
	glb_env.c      \
	glb_lib.c

libglb_la_LDFLAGS = -version-info 1:0:0

# for make dist
include_HEADERS = *.h

.PHONY: all clean

tags: TAGS

