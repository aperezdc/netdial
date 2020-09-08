/*
 * netdial.h
 * Copyright (C) 2020 Adrian Perez de Castro <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef NETDIAL_H
#define NETDIAL_H

/*
 * ADDRESS FORMAT: <type>:<node>[:<service>]
 *
 * The <type> field is mandatory and determines the address family and
 * connection type. It must be one of "unix", "unixp", "tcp", "udp", "tcp4",
 * "udp4", "tcp6", or "udp6".
 *
 * For "unix" and "unixp" sockets the <node> must be the socket path; while
 * <service> must be omitted. The "unixp" type choses SEQPACKET instead of
 * STREAM.
 *
 * For "tcp" and "udp" sockets the <node> is the address where to listen or
 * to connect to. The unversioned <type> names will choose either IPv4 or v6
 * depending on name resolution, while the versioned ones can be used to
 * explicitly choose the IP version used. When specifying IP addresses
 * directly, IPv6 addresses must be specified in between square brackets,
 * and zone names (and indexes) are supported with the usual syntax, using
 * a percent sign as separator.
 */

enum {
    NDdefault   = 0,

    NDblocking  = 1 << 1,
    NDexeckeep  = 1 << 2,

    /* Unix socket flags. */
    NDpasscred  = 1 << 9,
    NDpassec    = 1 << 10,

    /* Common socket flags. */
    NDbroadcast = 1 << 17,
    NDdebug     = 1 << 18,
    NDkeepalive = 1 << 19,
    NDreuseaddr = 1 << 20,
    NDreuseport = 1 << 21,
};

enum {
    NDclose = 0,
    NDread  = 1 << 1,
    NDwrite = 1 << 2,
    NDrdwr  = NDread | NDwrite,
};

extern int netdial(const char *address, int flags);
extern int netannounce(const char *address, int flags, int backlog);
extern int netaccept(int fd, int flags, char **remoteaddr);
extern int nethangup(int fd, int flags);

#endif /* !NETDIAL_H */
