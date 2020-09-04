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


struct flagaction {
    const char *name;
    int flag;
};

#ifndef SO_PASSCRED
#define SO_PASSCRED 0
#endif /* !SO_PASSCRED */

#ifndef SO_PASSEC
#define SO_PASSEC 0
#endif /* !SO_PASSEC */

static const struct flagaction unixflags[] = {
    { "passcred", .flag = SO_PASSCRED },
    { "passec",   .flag = SO_PASSEC   },
};

static const struct flagaction inetflags[] = {
    { "broadcast", .flag = SO_BROADCAST },
    { "debug",     .flag = SO_DEBUG     },
    { "keepalive", .flag = SO_KEEPALIVE },
    { "reuseaddr", .flag = SO_REUSEADDR },
    { "reuseport", .flag = SO_REUSEPORT },
};

static const struct flagaction*
getflag(const struct flagaction flags[], uint8_t nflags,
        const char *name, unsigned namelen)
{
    assert(name);
    for (unsigned i = 0; i < nflags; i++)
        if (strncasecmp(name, flags[i].name, namelen) == 0)
            return &flags[i];
    return NULL;
}

#define maxflags ((nelem(unixflags) > nelem(inetflags) \
                   ? nelem(unixflags) : nelem(inetflags)))

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
    const struct flagaction *flags[maxflags];
    uint8_t nflags;
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

    /* Port + flags are optional for Unix sockets. */
    if (!colon)
        return na->family == AF_UNIX;

    const char *service = ++colon;
    colon = strchr(service, ':');
    const unsigned servicelen = colon ? colon - service : strlen(service);
    if (servicelen > NI_MAXSERV)
        return false;

    memcpy(na->service, service, servicelen);
    na->service[servicelen] = '\0';
    na->servlen = servicelen;

    /* Flags re optional. */
    if (!colon)
        return true;

    const struct flagaction *flags =
        (na->family == AF_UNIX) ? unixflags : inetflags;
    unsigned nflags =
        (na->family == AF_UNIX) ? nelem(unixflags) : nelem(inetflags);

    const char *name = ++colon;
    do {
        colon = strchr(name, ',');
        unsigned namelen = colon ? colon - name : strlen(name);
        const struct flagaction *flag = getflag(flags, nflags, name, namelen);
        if (!flag)
            return false;

        /* Check whether the flag is already in the array, skip if so. */
        for (uint8_t i = 0; i < na->nflags; i++)
            if (na->flags[i] == flag)
                continue;

        /* Add the flag. */
        assert(na->nflags < maxflags);
        na->flags[na->nflags++] = flag;
    } while (colon);

    return true;
}

static bool
applyflags(const struct netaddr *na, int fd)
{
    assert(na);
    assert(fd >= 0);

    for (uint8_t i = 0; i < na->nflags; i++) {
        const struct flagaction *fa = na->flags[i];
        if (fa->flag) {
            static const int value = 1;
            if (setsockopt(fd, SOL_SOCKET, fa->flag, &value, sizeof(value)))
                return false;
        } else {
            /* TODO: Flag is unsupported in this build, log warning. */
        }
    }
    return true;
}

static int
unixsocket(const struct netaddr *na, int flag, struct sockaddr_un *name)
{
    assert(na);
    assert(name);

    if (na->addrlen >= sizeof(name->sun_path)) {
        errno = ERANGE;
        return -1;
    }

    *name = (struct sockaddr_un) { .sun_family = AF_UNIX };
    strncpy(name->sun_path, na->address, na->addrlen);

    int socktype = na->socktype;
    if (flag & NetdialCloexec)
        socktype |= SOCK_CLOEXEC;
    if (flag & NetdialNonblock)
        socktype |= SOCK_NONBLOCK;

    return socket(AF_UNIX, socktype, 0);
}

static int
netdialunix(const struct netaddr *na, int flag)
{
    struct sockaddr_un name;
    int fd = unixsocket(na, flag, &name);
    if (fd < 0)
        return -1;

    if (connect(fd, (const struct sockaddr*) &name, sizeof(name)) < 0) {
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
inetsocket(const struct netaddr *na, int flag,
           int (*op)(int, const struct sockaddr*, socklen_t))
{
    int sockflags = 0;
    if (flag & NetdialCloexec)
        sockflags |= SOCK_CLOEXEC;
    if (flag & NetdialNonblock)
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

static inline int
netdialinet(const struct netaddr *na, int flag)
{
    return inetsocket(na, flag, connect);
}

int
netdial(const char *address, int flag)
{
    struct netaddr na;
    if (!netaddrparse(address, &na))
        return -1;

    int fd = (na.family == AF_UNIX)
        ? netdialunix(&na, flag)
        : netdialinet(&na, flag);

    if (fd >= 0 && !applyflags(&na, fd)) {
        close(fd);
        return -1;
    }

    return fd;
}

static int
netannounceunix(const struct netaddr *na, int flag)
{
    struct sockaddr_un name;
    int fd = unixsocket(na, flag, &name);
    if (fd < 0)
        return -1;

    if (bind(fd, (const struct sockaddr*) &name, sizeof(name))) {
        close(fd);
        return -1;
    }

    return fd;
}

static inline int
netannounceinet(const struct netaddr *na, int flag)
{
    return inetsocket(na, flag, bind);
}

int
netannounce(const char *address, int flag, int backlog)
{
    struct netaddr na;
    if (!netaddrparse(address, &na)) {
        errno = EINVAL;
        return -1;
    }

    int fd = (na.family == AF_UNIX)
        ? netannounceunix(&na, flag)
        : netannounceinet(&na, flag);

    if (fd < 0)
        return -1;

    if (!applyflags(&na, fd) || listen(fd, (backlog > 0) ? backlog : 5) < 0) {
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

    const char *netname = getnetname(AF_UNIX, socktype);
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
netaccept(int fd, int flag, char **remoteaddr)
{
    assert(fd >= 0);

    struct sockaddr_storage sa = {};
    socklen_t salen = sizeof(sa);
    int nfd = accept4(fd, (struct sockaddr*) &sa, &salen,
                      (flag & NetdialNonblock) ? SOCK_NONBLOCK : 0 |
                      (flag & NetdialCloexec) ? SOCK_CLOEXEC : 0);
    if (nfd < 0)
        return -1;

    if (remoteaddr)
        *remoteaddr = mknetaddr(nfd, &sa, salen);

    return nfd;
}

int
netclose(int fd, int flag)
{
    assert(fd >= 0);

    switch (flag & NetdialReadwrite) {
        /* Half-close. */
        case NetdialRead:
            return shutdown(fd, SHUT_RD);
        case NetdialWrite:
            return shutdown(fd, SHUT_WR);
        case NetdialRead | NetdialWrite:
            return shutdown(fd, SHUT_RDWR);

        case NetdialClose: {
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
