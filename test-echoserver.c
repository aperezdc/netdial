/*
 * test-echoserver.c
 * Copyright (C) 2020 Adrian Perez de Castro <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#include "netdial.h"
#include <errno.h>
#include <event.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct conndata {
    struct event ev;
    char buffer[256];
    uint8_t size;
    size_t nbytes;
};

static void
handle_socket_rdwr(int fd, short events, void *data)
{
    struct conndata *conn = data;

    if (!conn->size && (events & EV_READ)) {
        /* Read data. */
        ssize_t r = read(fd, conn->buffer, sizeof(conn->buffer));
        fprintf(stderr, "[#%d] Read %zd bytes.\n", fd, r);

        if (r <= 0) {
            /* EOF or error: Close connection. */
            const char *errstr = strerror(errno);

            event_del(&conn->ev);
            nethangup(fd, NDclose);

            if (r < 0)
                fprintf(stderr, "[#%d] Closed, error: %s.\n", fd, errstr);
            else
                fprintf(stderr, "[#%d] Closed, exchanged %zu bytes.\n", fd, conn->nbytes);

            free(conn);
            return;
        }

        conn->size = r;
    }

    if (conn->size && (events & EV_WRITE)) {
        /* Write pending data. */
        ssize_t r = write(fd, conn->buffer, conn->size);
        fprintf(stderr, "[#%d] Wrote %zd bytes, %zd pending.\n",
                fd, r, conn->size - r);

        if (r <= 0) {
            fprintf(stderr, "[#%d] Write: %s.\n", fd, strerror(errno));
            return;
        }

        conn->nbytes += r;
        conn->size -= r;

        if (r < conn->size)
            memmove(conn->buffer, conn->buffer + r, conn->size);
    }
}

static void
handle_accept(int fd, short events, void *data)
{
    char *remote = NULL;
    int nfd = netaccept(fd, NDdefault, &remote);
    if (nfd < 0) {
        fprintf(stderr, "Netaccept: %s.\n", strerror(errno));
        return;
    }

    fprintf(stderr, "[#%d] New connection <%s>\n", nfd, remote);
    free(remote);

    struct conndata *conn = calloc(1, sizeof(struct conndata));
    event_set(&conn->ev, nfd, EV_READ | EV_WRITE | EV_PERSIST,
              handle_socket_rdwr, conn);
    event_add(&conn->ev, NULL);
}

static void
handle_signal(int signum, short events, void *data)
{
    fprintf(stderr, "Exiting gracefully...\n");
    signal_del(data);
    event_loopexit(NULL);
}

int
main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <address>\n", argv[0]);
        fprintf(stderr, "Example: %s tcp:localhost:echo\n", argv[0]);
        return EXIT_FAILURE;
    }

    setlinebuf(stderr);

    int fd = netannounce(argv[1], NDdefault, 0);
    if (fd < 0) {
        fprintf(stderr, "Cannot announce %s: %s.\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    struct event_base *evbase = event_init();

    struct event evaccept;
    event_set(&evaccept, fd, EV_READ | EV_PERSIST, handle_accept, NULL);
    event_add(&evaccept, NULL);

    struct event evsignal;
    signal_set(&evsignal, SIGINT, handle_signal, &evsignal);
    signal_add(&evsignal, NULL);

    event_dispatch();
    event_base_free(evbase);

    nethangup(fd, NDclose);
    return EXIT_SUCCESS;
}
