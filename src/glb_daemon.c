/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * The code below is adapted from
 * http://www.itp.uzh.ch/~dpotter/howto/daemonize
 * which is public domain.
 *
 * $Id: glb_daemon.c 161 2013-11-03 14:54:45Z alex $
 */

#include "glb_daemon.h"
#include "glb_log.h"
#include "glb_signal.h"
#include "glb_cnf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>

/* Change this to the user under which to run */
#define RUN_AS_USER "daemon"

#define CHILD_OK_TIMEOUT 5

void glb_daemon_start (const glb_cnf_t* cnf)
{
    pid_t pid, sid;

    /* already a daemon */
    if (getppid() == 1) return;

    /* Drop user if there is one, and we run as root */
    if (getuid() == 0 || geteuid() == 0) {
        struct passwd *pw = getpwnam(RUN_AS_USER);
        if ( pw ) {
            glb_log_info ("Changing effective user to '%s'", RUN_AS_USER);
            if (setgid(pw->pw_gid))
            {
                glb_log_fatal ("Failed to change group: %d (%s)",
                               errno, strerror(errno));
                exit(EXIT_FAILURE);
            }
            if (setuid(pw->pw_uid))
            {
                glb_log_fatal ("Failed to change user: %d (%s)",
                               errno, strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
    }

    /* Fork off the parent process */
    pid = fork();
    if (pid < 0) {
        glb_log_fatal ("Unable to fork daemon: %d (%s)",
                       errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* If we got a good PID, then we can exit the parent process. */
    if (pid > 0) {
        /* Wait for confirmation from the child via GLB_SIGNAL_OK, or
           for SIGALRM.  If pause() returns - it means timeout. */
        alarm(CHILD_OK_TIMEOUT);
        pause();
        glb_log_fatal ("Timeout waiting for child process confirmation.");
        exit (EXIT_FAILURE);
    }

    /* At this point we are executing as the child process */

    /* Cancel certain signals */
//    signal(SIGCHLD,SIG_DFL); /* A child process dies */
    signal(SIGTSTP,SIG_IGN); /* Various TTY signals */
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
//    signal(SIGHUP, SIG_IGN); /* Ignore hangup signal */
//    signal(SIGTERM,SIG_DFL); /* Die on SIGTERM */

    /* Change the file mode mask */
    umask(0);

    /* Create a new SID for the child process */
    sid = setsid();
    if (sid < 0) {
        syslog(LOG_ERR, "unable to create a new session, code %d (%s)",
               errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Change the current working directory.  This prevents the current
       directory from being locked; hence not being able to remove it. */
    if ((chdir("/")) < 0) {
        syslog(LOG_ERR, "unable to change directory to %s, code %d (%s)",
               "/", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (glb_log_init (GLB_LOG_SYSLOG, cnf->verbose)) exit (EXIT_FAILURE);

    /* Redirect standard files to /dev/null */
    if (NULL == freopen( "/dev/null", "r", stdin)  ||
        NULL == freopen( "/dev/null", "w", stdout) ||
        NULL == freopen( "/dev/null", "w", stderr)) {
        syslog (LOG_ERR, "freopen (/dev/null) failed: %d (%s)", errno,
                strerror (errno));
        exit (EXIT_FAILURE);
    }
}

void
glb_daemon_ok ()
{
    pid_t parent;

    /* Notify the parent process that we are A-okay */
    parent = getppid();
    kill(parent, GLB_SIGNAL_OK);
}
