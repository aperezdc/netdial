/*
 * netdial.h
 * Copyright (C) 2020 Adrian Perez de Castro <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef NETDIAL_H
#define NETDIAL_H

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
    /* Hangup flags. */
    NDclose = 0,
    NDread  = 1 << 1,
    NDwrite = 1 << 2,
    NDrdwr  = NDread | NDwrite,
};

enum {
    /* Address kind. */
    NDlocal,
    NDremote,
};

extern int netdial(const char *address, int flags);
extern int netannounce(const char *address, int flags, int backlog);
extern int netaccept(int fd, int flags, char **remoteaddr);
extern int nethangup(int fd, int flags);
extern int netaddress(int fd, int kind, char **address);

#endif /* !NETDIAL_H */
