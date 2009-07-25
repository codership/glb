#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.50)
AC_INIT(glb, 0.7.3, info@codership.com)
AC_CONFIG_SRCDIR([src/glb_main.c])
AC_CONFIG_HEADER([config.h])
AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE
#AM_GNU_GETTEXT
#AM_GNU_GETTEXT_VERSION(0.17)

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
    CFLAGS="$CFLAGS -D_GNU_SOURCE -DGLB_USE_SPLICE"
fi
AM_CONDITIONAL(ENABLE_SPLICE, test "$enable_splice" != "no")

# Check for debug
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--enable-debug],
	                    [enable debugging code [[default=disabled]]]),,
	      enable_debug="no")
if test "$enable_debug" == "yes"
then
    CFLAGS="-g -O0 -fno-inline "
else
    CFLAGS=${CFLAGS:-" -O3 -mtune=i686 "}
    CFLAGS="$CFLAGS -DNDEBUG "
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

# Check for stats
AC_ARG_ENABLE(stats,
              AC_HELP_STRING([--enable-stats],
	                    [use extensive pool statistics [[default=disabled]]]),,
	      enable_stats="no")
if test "$enable_stats" == "yes"
then
    CFLAGS="$CFLAGS -DGLB_POOL_STATS"
fi
AM_CONDITIONAL(ENABLE_STATS, test "$enable_stats" != "no")


# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
#AC_PROG_LIBTOOL

AC_CHECK_LIB([pthread], [pthread_testcancel],,
             AC_MSG_ERROR([*** POSIX threads not found! ***]))
	     
# Checks for header files.
AC_CHECK_HEADERS([strings.h sys/time.h unistd.h])

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
CFLAGS="$CFLAGS -Wall -Werror -DUSE_$POLL "
AC_SUBST(CFLAGS)
#CXXFLAGS="$CFLAGS $CXXFLAGS"
#AC_SUBST(CXXFLAGS)

AC_MSG_NOTICE([----------------------])
AC_MSG_NOTICE([poll method used: $POLL])
AC_MSG_NOTICE([splice() enabled: $enable_splice])
AC_MSG_NOTICE([stats    enabled: $enable_stats])
AC_MSG_NOTICE([debug    enabled: $enable_debug])
AC_MSG_NOTICE([CFLAGS = $CFLAGS])
AC_MSG_NOTICE([----------------------])

AC_CONFIG_FILES([Makefile
                 doc/Makefile
                 src/Makefile])
AC_OUTPUT
