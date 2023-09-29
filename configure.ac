#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.50)
AC_INIT(glb, 1.0.1, info@codership.com)
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_SRCDIR([src/glb_main.c])
AC_CONFIG_HEADER([config.h])
AC_CANONICAL_SYSTEM

case $host_os in freebsd*)
    : ${CC:="gcc48"}
    : ${CXX:="g++48"}
    : ${LD_LIBRARY_PATH:=/usr/local/lib/gcc48}
esac

AM_INIT_AUTOMAKE
CFLAGS="$CFLAGS" # prevent configure from guessing its own CFLAGS

# Check for debug
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--enable-debug],
                             [enable debugging code [[default=disabled]]]),,
              enable_debug="no")
if test "$enable_debug" == "yes"
then
    AM_CFLAGS="-g -O0 -fno-inline"
else
    AM_CFLAGS="-g -O3"
    AM_CPPFLAGS="-DNDEBUG"
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

# Check for _GNU_SOURCE
AC_ARG_ENABLE(gnu,
              AC_HELP_STRING([--disable-gnu],
                             [disable GNU extensions (disables libglb) [[default=enabled]]]),,
              enable_gnu="yes")
if test "$enable_gnu" != "no"
then
    AM_CPPFLAGS="$AM_CPPFLAGS -D_GNU_SOURCE"
else
    AC_MSG_WARN([You have chosen to disable GNU libc extensions.
    This disables libc call overloading and makes libglb impossible.])
fi
AM_CONDITIONAL(BUILD_LIBGLB, test "$enable_gnu" != "no")

# Check for poll method
AC_ARG_ENABLE([poll],
              [AC_HELP_STRING([--enable-poll],
                              [use poll() for polling (default: use epoll() if available)]
                             )
              ],
              [],
              enable_poll="no")

# Check for splice()
AC_ARG_ENABLE(splice,
              AC_HELP_STRING([--enable-splice],
                             [use splice() [[default=disabled]]]),,
              enable_splice="no")
if test "$enable_splice" == "yes"
then
    AM_CPPFLAGS="$AM_CPPFLAGS -DGLB_USE_SPLICE"
fi
AM_CONDITIONAL(ENABLE_SPLICE, test "$enable_splice" != "no")

# Check for stats
AC_ARG_ENABLE(stats,
              AC_HELP_STRING([--enable-stats],
                            [use extensive pool statistics [[default=disabled]]]),,
              enable_stats="no")
if test "$enable_stats" == "yes"
then
    AM_CPPFLAGS="$AM_CPPFLAGS -DGLB_POOL_STATS"
fi
AM_CONDITIONAL(ENABLE_STATS, test "$enable_stats" != "no")


# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
LT_INIT # AC_PROG_LIBTOOL

# Checks for header files.
AC_CHECK_HEADERS([strings.h sys/time.h unistd.h dlfcn.h])

AC_CHECK_LIB([pthread], [pthread_testcancel],,
             AC_MSG_ERROR([*** POSIX threads not found! ***]))

case $host_os in freebsd*) ;; *)
    AC_CHECK_LIB([dl], [dlsym],,
                 AC_MSG_ERROR([*** libdl not found! ***]))
    ;;
esac

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_HEADER_STDC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([gettimeofday memset strdup strcasecmp strncasecmp strtol])

# Check for poll()/epoll()
AC_CHECK_HEADERS([poll.h],
                 [AC_CHECK_FUNC([poll],[POLL="POLL"],
                                [AC_MSG_FAILURE([*** poll() not found! ***])]
                               )
                 ],
                 [AC_MSG_FAILURE([*** poll.h not found! ***])]
                )

if test "$enable_poll" == "no"
then
    AC_CHECK_HEADERS([sys/epoll.h],
                     [AC_CHECK_FUNC([epoll_create],
                                    [POLL="EPOLL"],
                                    [AC_MSG_WARN([epoll API not found, trying poll()])]
                                   )
                     ]
                    )
fi

if test "$enable_splice" == "yes"
then
AC_CHECK_FUNCS([splice])
fi

# Many feature checks are broken and issue warnings.
# If we want checks to pass we have to put this at the very end.
AM_CFLAGS="$AM_CFLAGS -Wall -Werror"
AM_CPPFLAGS="$AM_CPPFLAGS -DUSE_$POLL"

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CPPFLAGS)

AC_MSG_NOTICE([----------------------])
AC_MSG_NOTICE([poll method used: $POLL])
AC_MSG_NOTICE([splice() enabled: $enable_splice])
AC_MSG_NOTICE([stats    enabled: $enable_stats])
AC_MSG_NOTICE([debug    enabled: $enable_debug])
AC_MSG_NOTICE([CFLAGS   = $AM_CFLAGS $CFLAGS])
AC_MSG_NOTICE([CPPFLAGS = $AM_CPPFLAGS $CPPFLAGS])
AC_MSG_NOTICE([----------------------])

AC_CONFIG_FILES([Makefile
                 doc/Makefile
                 src/Makefile])

AC_OUTPUT
