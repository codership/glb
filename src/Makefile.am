# Copyright (C) 2008-2012 Codership Oy <info@codership.com>
# $Id$

sbin_PROGRAMS = glbd

glbd_SOURCES = \
	glb_log.c      \
	glb_signal.c   \
	glb_daemon.c   \
	glb_socket.c   \
	glb_dst.c      \
	glb_cnf.c      \
	glb_cmd.c      \
	glb_router.c   \
	glb_pool.c     \
	glb_listener.c \
	glb_control.c  \
	glb_limits.c   \
	glb_main.c     \
	*.h
glbd_CFLAGS = $(AM_CFLAGS) -DGLBD

lib_LTLIBRARIES   = libglb.la

libglb_la_SOURCES = \
	glb_socket.c   \
	glb_dst.c      \
	glb_cnf.c      \
	glb_env.c      \
	glb_router.c   \
	glb_control.c  \
	glb_lib.c

libglb_la_LDFLAGS = -version-info 0:9:0

.PHONY: all clean

tags: TAGS

