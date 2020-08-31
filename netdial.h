/*
 * netdial.h
 * Copyright (C) 2020 Adrian Perez de Castro <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef NETDIAL_H
#define NETDIAL_H

/*
 * ADDRESS FORMAT: <type>:<node>[:<port>[:<flags>]]
 *
 * The <type> field is mandatory and determines the address family and
 * connection type. It must be one of "unix", "tcp", or "udp".
 *
 * For "unix" sockets the <node> must be the path to socket node; while
 * <port> must always be empty and <flags> is a comma-separated list of
 * zero or more of the following:
 *
 */

enum {
    NetdialNone = 0,
    NetdialCloexec = 1 << 1,
    NetdialNonblock = 1 << 2,
};

extern int netdial(const char *address, int flag);
extern int netannounce(const char *address, int flag);

#endif /* !NETDIAL_H */
