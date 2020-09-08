/*
 * netdial.c
 * Copyright (C) 2020 Adrian Perez de Castro <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#define _POSIX_C_SOURCE 201112L
#define _DEFAULT_SOURCE

#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__)
# define AUTODETECTED_ACCEPT4 1
# define _BSD_SOURCE
#endif /* __*BSD__ */

#if defined(__linux__)
# define AUTODETECTED_ACCEPT4 1
#endif /* __linux__ */

#if !defined(AUTODETECTED_ACCEPT4)
# define AUTODETECTED_ACCEPT4 0
#endif /* !AUTODETECTED_ACCEPT4 */

#if !defined(HAVE_ACCEPT4)
# define HAVE_ACCEPT4 AUTODETECTED_ACCEPT4
#endif /* !HAVE_ACCEPT4 */

#include "dbuf/dbuf.h"
#include "netdial.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef nelem
#define nelem(v) (sizeof(v) / sizeof(v[0]))
#endif /* !nelem */

#if HAVE_ACCEPT4
extern int accept4(int, struct sockaddr*, socklen_t*, int);
#else /* !HAVE_ACCEPT4 */
static inline int
accept4(int fd, struct sockaddr *sa, socklen_t *salen, int flags)
{
    int nfd = accept(fd, sa, salen);
    if (nfd < 0)
        return nfd;

    const int of = fcntl(nfd, F_GETFD);
    if (of == -1)
        goto beach;

    int nf = of;
    if (flags & SOCK_NONBLOCK)
        nf |= O_NONBLOCK;
    else
        nf &= ~O_NONBLOCK;

    if (nf != of)
        if (fcntl(nfd, F_SETFD, nf) == -1)
            goto beach;

    return nfd;

beach:
    close(nfd);
    return -1;
}
#endif /* HAVE_ACCEPT4 */

#ifndef SO_PASSCRED
#define SO_PASSCRED 0
#endif /* !SO_PASSCRED */

#ifndef SO_PASSEC
#define SO_PASSEC 0
#endif /* !SO_PASSEC */

static const struct {
    int ndflag;
    int sockopt;
} optflags[] = {
    { NDpasscred,  SO_PASSCRED  },
    { NDpassec,    SO_PASSEC    },
    { NDbroadcast, SO_BROADCAST },
    { NDdebug,     SO_DEBUG     },
    { NDkeepalive, SO_KEEPALIVE },
    { NDreuseaddr, SO_REUSEADDR },
    { NDreuseport, SO_REUSEPORT },
};

enum {
    NDsockflagmask = 0x000000FF,
    NDunixoptmask  = 0x0000FF00,
    NDsockoptmask  = 0xFFFF0000,
};

static const struct {
    const char *name;
    int family, socktype;
} nettypes[] = {
    { "tcp",   AF_UNSPEC, SOCK_STREAM    },
    { "udp",   AF_UNSPEC, SOCK_DGRAM     },
    { "tcp4",  AF_INET,   SOCK_STREAM    },
    { "udp4",  AF_INET,   SOCK_DGRAM     },
    { "tcp6",  AF_INET6,  SOCK_STREAM    },
    { "udp6",  AF_INET6,  SOCK_DGRAM     },
    { "unix",  AF_UNIX,   SOCK_STREAM    },
    { "unixp", AF_UNIX,   SOCK_SEQPACKET },
};

static bool
getnettype(const char *name, unsigned namelen, int *family, int *socktype)
{
    assert(name);
    assert(family);
    assert(socktype);

    if (!namelen)
        return false;

    for (unsigned i = 0; i < nelem(nettypes); i++) {
        if (strncasecmp(name, nettypes[i].name, namelen) == 0) {
            *family = nettypes[i].family;
            *socktype = nettypes[i].socktype;
            return true;
        }
    }

    return false;
}

static const char*
getnetname(int family, int socktype)
{
    for (unsigned i = 0; i < nelem(nettypes); i++)
        if (nettypes[i].family == family && nettypes[i].socktype == socktype)
            return nettypes[i].name;
    return NULL;
}

struct netaddr {
    int family, socktype;
    char address[NI_MAXHOST + 1];
    char service[NI_MAXSERV + 1];
    uint16_t addrlen;
    uint16_t servlen;
};

static bool
netaddrparse(const char *str, struct netaddr *na)
{
    *na = (struct netaddr) {};

    const char *type = str;
    const char *colon = strchr(type, ':');
    if (!colon)
        return false;

    const unsigned typelen = colon - type;
    if (!getnettype(type, typelen, &na->family, &na->socktype))
        return false;

    const char *node = ++colon;
    unsigned nodelen;
    if (*node == '[') {
        /* Bracketed IPv6 address. */
        const char *endbracket = strchr(++node, ']');
        if (!endbracket)
            return false;
        nodelen = endbracket++ - node;
        if (*endbracket != ':')
            return false;
        colon = endbracket;
        if (na->family == AF_UNSPEC)
            na->family = AF_INET6;
        else if (na->family != AF_INET6)
            return false;
    } else {
        colon = strchr(node, ':');
        nodelen = colon ? colon - node : strlen(node);
    }
    if (nodelen > NI_MAXHOST)
        return false;

    memcpy(na->address, node, nodelen);
    na->address[nodelen] = '\0';
    na->addrlen = nodelen;

    /* Port is optional for Unix sockets. */
    if (!colon)
        return na->family == AF_UNIX;

    const char *service = ++colon;
    const unsigned servicelen = strlen(service);
    if (servicelen > NI_MAXSERV)
        return false;

    memcpy(na->service, service, servicelen);
    na->service[servicelen] = '\0';
    na->servlen = servicelen;

    return true;
}

static bool
applyflags(int fd, int flags)
{
    assert(fd >= 0);

    for (unsigned i = 0; i < nelem(optflags); i++) {
        if (optflags[i].sockopt == 0) {
            /* TODO: Flag is unsupported in this build, log warning. */
            continue;
        }

        if (flags & optflags[i].ndflag) {
            static const int value = 1;
            if (setsockopt(fd, SOL_SOCKET, optflags[i].sockopt, &value, sizeof(value)))
                return false;
        }
    }

    return true;
}

static int
unixsocket(const struct netaddr *na, int flags,
           int (*op)(int, const struct sockaddr*, socklen_t))
{
    assert(na);
    assert(op);

    struct sockaddr_un name = { .sun_family = AF_UNIX };
    if (na->addrlen >= sizeof(name.sun_path)) {
        errno = ERANGE;
        return -1;
    }
    strncpy(name.sun_path, na->address, na->addrlen);

    int socktype = na->socktype;
    if (!(flags & NDexeckeep))
        socktype |= SOCK_CLOEXEC;
    if (!(flags & NDblocking))
        socktype |= SOCK_NONBLOCK;

    int fd = socket(AF_UNIX, socktype, 0);
    if (fd < 0)
        return -1;

    if ((*op)(fd, (const struct sockaddr*) &name, sizeof(name)) == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

static struct addrinfo*
netaddrinfo(const struct netaddr *na, int *errcode, bool listen)
{
    const struct addrinfo hints = {
        .ai_family = na->family,
        .ai_socktype = na->socktype,
        .ai_flags = (listen ? AI_PASSIVE : 0),
    };

    struct addrinfo *result = NULL;
    if ((*errcode = getaddrinfo(na->addrlen ? na->address : NULL,
                                na->service, &hints, &result))) {
        if (result)
            freeaddrinfo(result);
        return NULL;
    }

    return result;
}

static int
inetsocket(const struct netaddr *na, int flags,
           int (*op)(int, const struct sockaddr*, socklen_t))
{
    int sockflags = 0;
    if (!(flags & NDexeckeep))
        sockflags |= SOCK_CLOEXEC;
    if (!(flags & NDblocking))
        sockflags |= SOCK_NONBLOCK;

    /* TODO: Use "errcode" for something. */
    int errcode;
    struct addrinfo *ra = netaddrinfo(na, &errcode, op == bind);
    if (!ra)
        return -1;

    int fd = -1;
    struct addrinfo *ai;
    for (ai = ra; ai; ai = ai->ai_next) {
        const int socktype = ai->ai_socktype | sockflags;
        if ((fd = socket(ai->ai_family, socktype, ai->ai_protocol)) == -1)
            continue;

        if ((*op)(fd, ai->ai_addr, ai->ai_addrlen) != -1)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(ra);
    return fd;
}

int
netdial(const char *address, int flags)
{
    struct netaddr na;
    if (!netaddrparse(address, &na))
        return -1;

    int fd;
    if (na.family == AF_UNIX) {
        fd = unixsocket(&na, flags, connect);
    } else {
        flags &= ~NDunixoptmask;
        fd = inetsocket(&na, flags, connect);
    }

    if (fd >= 0 && !applyflags(fd, flags)) {
        close(fd);
        return -1;
    }

    return fd;
}

int
netannounce(const char *address, int flags, int backlog)
{
    struct netaddr na;
    if (!netaddrparse(address, &na)) {
        errno = EINVAL;
        return -1;
    }

    int fd;
    if (na.family == AF_UNIX) {
        fd = unixsocket(&na, flags, bind);
    } else {
        flags &= ~NDunixoptmask;
        fd = inetsocket(&na, flags, bind);
    }

    if (fd < 0)
        return -1;

    if (!applyflags(fd, flags) || listen(fd, (backlog > 0) ? backlog : 5) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static char*
mknetaddr(int fd, const struct sockaddr_storage *sa, socklen_t salen)
{
    int socktype;
    socklen_t socktypelen = sizeof(socktype);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socktype, &socktypelen))
        socktype = SOCK_STREAM;

    const char *netname = getnetname(sa->ss_family, socktype);
    if (!netname)
        return NULL;

    struct dbuf b = DBUF_INIT;
    dbuf_addstr(&b, netname);
    dbuf_addch(&b, ':');

    switch (sa->ss_family) {
        case AF_UNIX:
            dbuf_addmem(&b, ((const struct sockaddr_un*) sa)->sun_path, salen);
            break;
        case AF_INET:
        case AF_INET6: {
            char host[NI_MAXHOST + 1];
            char serv[NI_MAXSERV + 1];
            if (getnameinfo((const struct sockaddr*) sa, salen,
                            host, sizeof(host),
                            serv, sizeof(serv),
                            socktype == SOCK_DGRAM ? NI_DGRAM : 0 |
                            NI_NUMERICHOST |
                            NI_NUMERICSERV)) {
                dbuf_clear(&b);
                return NULL;
            }

            dbuf_addstr(&b, host);
            dbuf_addch(&b, ':');
            dbuf_addstr(&b, serv);
            break;
        }
    }

    return dbuf_str(&b);
}

int
netaccept(int fd, int flags, char **remoteaddr)
{
    assert(fd >= 0);

    struct sockaddr_storage sa = {};
    socklen_t salen = sizeof(sa);
    int nfd = accept4(fd, (struct sockaddr*) &sa, &salen,
                      (flags & NDblocking) ? 0 : SOCK_NONBLOCK |
                      (flags & NDexeckeep) ? 0 : SOCK_CLOEXEC);
    if (nfd < 0)
        return -1;

    if (remoteaddr)
        *remoteaddr = mknetaddr(nfd, &sa, salen);

    return nfd;
}

int
nethangup(int fd, int flags)
{
    assert(fd >= 0);

    switch (flags & NDrdwr) {
        /* Half-close. */
        case NDread:
            return shutdown(fd, SHUT_RD);
        case NDwrite:
            return shutdown(fd, SHUT_WR);
        case NDrdwr:
            return shutdown(fd, SHUT_RDWR);

        case NDclose: {
            int listen = 0;
            socklen_t len = sizeof(listen);
            if (getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &listen, &len))
                return -1;

            struct sockaddr_storage ss;
            if (listen) {
                len = sizeof(ss);
                if (getsockname(fd, (struct sockaddr*) &ss, &len))
                    return -1;
            }

            if (close(fd))
                return -1;

            if (listen && ss.ss_family == AF_UNIX) {
                /* XXX: .sun_path it might not be null terminated. */
                char node[len + 1];
                strncpy(node, ((struct sockaddr_un*) &ss)->sun_path, len);
                return unlink(node);
            }
            break;
        }
    }

    return 0;
}
