/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
 *
 * $Id: glb_socket.c 161 2013-11-03 14:54:45Z alex $
 */

#include "glb_socket.h"
#include "glb_cnf.h"
#include "glb_log.h"
#include "glb_misc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#ifndef GLBD
#include <arpa/inet.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__)
# define SOL_TCP IPPROTO_TCP
#endif

#ifdef GLBD

static const struct glb_cnf* glb_cnf = 0;

void
glb_socket_init(const glb_cnf_t* cnf)
{
    glb_cnf = cnf;
}

#endif /* GLBD */

// maximum IP address length = 21: aaa.bbb.ccc.ddd:ppppp
//                                                ^ position 15

glb_sockaddr_str_t
glb_sockaddr_to_str (const glb_sockaddr_t* addr)
{
    uint8_t* a = (void*)&addr->sin_addr.s_addr;
    uint16_t p = ntohs (addr->sin_port);
    glb_sockaddr_str_t ret = {{ 0, }};

    snprintf (ret.str, sizeof(ret.str),
              "%hhu.%hhu.%hhu.%hhu:%hu", a[0], a[1], a[2], a[3], p);

    return ret;
}

glb_sockaddr_str_t
glb_sockaddr_to_astr (const glb_sockaddr_t* addr)
{
    uint8_t* a = (void*)&addr->sin_addr.s_addr;
    uint16_t p = ntohs (addr->sin_port);
    glb_sockaddr_str_t ret = {{ 0, }};

    char ip[16];
    long ip_len;
    char port[7];
    long port_len;

    ip_len = snprintf (ip, 16, "%hhu.%hhu.%hhu.%hhu", a[0], a[1], a[2], a[3]);
    assert (ip_len < 16);

    port_len = snprintf (port, 7, ":%hu", p);
    assert (port_len < 7);

    snprintf (ret.str, sizeof(ret.str), "                     ");

    // make so that ':' is on position 15
    memcpy (ret.str + 15 - ip_len, ip, ip_len);
    memcpy (ret.str + 15, port, port_len);

    return ret;
}

// Initialize glb_sockaddr_t struct
long
glb_sockaddr_init (glb_sockaddr_t* addr,
                   const char*     hostname,
                   uint16_t        port)
{
#ifdef GLBD
    struct hostent* host = gethostbyname (hostname);

    if (host == NULL)
    {
        glb_log_error ("Unknown host %s.\n", hostname);
        return -EINVAL;
    }

    memset (addr, 0, sizeof(*addr));
    addr->sin_addr   = *(struct in_addr *) host->h_addr;
#else
    memset (addr, 0, sizeof(*addr));
    if (inet_aton(hostname, &addr->sin_addr) != 1)
        return -EINVAL;
#endif /* GLBD */

    addr->sin_port   = htons (port);
    addr->sin_family = AF_INET;

    return 0;
}

void
glb_sockaddr_set_port (glb_sockaddr_t* addr, uint16_t port)
{
    addr->sin_port   = htons (port);
}

short
glb_sockaddr_get_port (const glb_sockaddr_t* addr)
{
    return ntohs (addr->sin_port);
}

glb_sockaddr_str_t
glb_sockaddr_get_host (const glb_sockaddr_t* addr)
{
    uint8_t* a = (void*)&addr->sin_addr.s_addr;
    glb_sockaddr_str_t ret = {{ 0, }};

    snprintf (ret.str, sizeof(ret.str) - 1,
              "%hhu.%hhu.%hhu.%hhu", a[0], a[1], a[2], a[3]);

    return ret;
}

#ifdef GLBD

#define FNV32_SEED  2166136261U
#define FNV32_PRIME 16777619U
#define ROTL32(x,r) ((x << r) | (x >> (32 - r)))

static inline uint32_t
fnv32a_mix(const void* buf, size_t buf_len)
{
    uint32_t ret = FNV32_SEED;
    const uint8_t* ptr = (uint8_t*)buf;
    const uint8_t* const end = ptr + buf_len;;

    while (ptr != end)
    {
        ret = (ret ^ *ptr) * FNV32_PRIME;
        ptr++;
    }

    /* mix to improve avalanche effect */
    ret *= ROTL32(ret, 24);
    return ret ^ ROTL32(ret, 21);
}

uint32_t
glb_sockaddr_hash (const glb_sockaddr_t* addr)
{
    return fnv32a_mix (&addr->sin_addr, sizeof(addr->sin_addr));
}

int
glb_socket_setopt (int sock, uint32_t const optflags)
{
//    int const zero = 0;
    int const one  = 1;
    int ret = 0;

#if 0
    size_t const buf_size = 1024;

    /* probably a good place to specify some socket options */
    if (setsockopt (sock, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)))
    {
        glb_log_error ("setsockopt() failed: %d (%s)", errno, strerror(errno));
        return -errno;
    }

    if (setsockopt (sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)))
    {
        glb_log_error ("setsockopt() failed: %d (%s)", errno, strerror(errno));
        return -errno;
    }
#endif

    if ((optflags & GLB_SOCK_KEEPALIVE) &&
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)))
    {
        glb_log_warn ("Setting SO_KEEPALIVE failed: %d (%s)",
                      errno, strerror(errno));
        ret = -errno;
    }
    else
    {
#if defined(__APPLE__)
        int idle_seconds = 10;
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPALIVE,
                        &idle_seconds, sizeof(idle_seconds)))
        {
            glb_log_warn ("Setting TCP_KEEPIDLE failed: %d (%s)",
                          errno, strerror(errno));
            ret = -errno;
        }
        // keep interval is system-wide and is equal to 75 secs by default
        // keep count is system-wide and is equal to 8 by default
#else /* !__APPLE__ */
# if defined(TCP_KEEPIDLE)
        int idle_seconds = 10;
        if (!setsockopt(sock, SOL_TCP, TCP_KEEPIDLE,
                        &idle_seconds, sizeof(idle_seconds)))
        {
# if defined(TCP_KEEPINTVL)
            int interval = 5;
            if (!setsockopt(sock, SOL_TCP, TCP_KEEPINTVL,
                            &interval, sizeof(interval)))
            {
# if defined(TCP_KEEPCNT)
                int tries = 3;
                if (setsockopt(sock, SOL_TCP, TCP_KEEPCNT,
                               &tries, sizeof(tries)))
                {
                    glb_log_warn ("Setting TCP_KEEPCNT failed: %d (%s)",
                                  errno, strerror(errno));
                    ret = -errno;
                }
# endif /* TCP_KEEPCNT */
            }
            else
            {
                glb_log_warn ("Setting TCP_KEEPINTVL failed: %d (%s)",
                              errno, strerror(errno));
                ret = -errno;
            }
# endif /* TCP_KEEPINTVL */
        }
        else
        {
            glb_log_warn ("Setting TCP_KEEPIDLE failed: %d (%s)",
                          errno, strerror(errno));
            ret = -errno;
        }
# endif /* TCP_KEEPIDLE */
#endif /* !__APPLE__ */
    }

    if ((optflags & GLB_SOCK_NODELAY) && glb_cnf->nodelay &&
        setsockopt(sock, SOL_TCP, TCP_NODELAY, &one, sizeof(one)))
    {
        glb_log_warn ("Setting TCP_NODELAY failed: %d (%s)",
                      errno, strerror(errno));
        ret = -errno;
    }

#if defined(TCP_DEFER_ACCEPT)
    if ((optflags & GLB_SOCK_DEFER_ACCEPT) && glb_cnf->defer_accept &&
        setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, &one, sizeof(one)))
    {
        glb_log_warn ("Setting TCP_DEFER_ACCEPT failed: %d (%s)",
                      errno, strerror(errno));
        ret = -errno;
    }
#endif /* TCP_DEFER_ACCEPT */

    if ((optflags & GLB_SOCK_NONBLOCK) &&
        glb_fd_setfl (sock, O_NONBLOCK, true))
    {
        glb_log_warn ("Setting O_NONBLOCK failed: %d (%s)",
                      errno, strerror(errno));
        ret = -errno;
    }

    if (glb_cnf->linger)
    {
        struct linger l = { 1, 0 };
        if (setsockopt (sock, SOL_SOCKET, SO_LINGER, &l, sizeof(l)))
        {
            glb_log_warn ("Setting SO_LINGER failed: %d (%s)",
                          errno, strerror(errno));
            ret = -errno;
        }
    }

    return ret;
}

#endif /* GLBD */

int
glb_socket_create (const struct sockaddr_in* addr, uint32_t const optflags)
{
    int sock;
    int err;

    /* Create the socket. We don't want CLOEXEC for libglb as we don't
     * know if application will fork. Libglb opens only control sockets. */
#if defined(SOCK_CLOEXEC) && defined(GLBD)
    sock = socket (PF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
    sock = socket (PF_INET, SOCK_STREAM, 0);
#endif
    if (sock < 0)
    {
        err = -errno;
        glb_log_error("Failed to create socket: %d (%s)", -err, strerror(-err));
        return err;
    }

#ifdef GLBD
#ifndef SOCK_CLOEXEC
    if ((err = glb_fd_setfd (sock, FD_CLOEXEC, true))) goto error;
#endif /* !SOCK_CLOEXEC */
    if ((err = glb_socket_setopt (sock, optflags)))    goto error;
#endif /* GLBD */

    if (bind (sock, (struct sockaddr *) addr, sizeof (*addr)) < 0)
    {
        err = -errno;
        glb_log_error ("Failed to bind socket: %d (%s)", -err, strerror(-err));
        goto error;
    }

    return sock;

error:

    close (sock);
    return err;
}

