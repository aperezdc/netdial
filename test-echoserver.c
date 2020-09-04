/*
 * test-echoserver.c
 * Copyright (C) 2020 Adrian Perez de Castro <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#include "netdial.h"
#include <errno.h>
#include <ev.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct conndata {
    ev_io watcher;
    char buffer[256];
    uint8_t size;
    size_t nbytes;
};

static void
handle_socket_rdwr(EV_P_ ev_io *w, int revents)
{
    struct conndata *conn = (struct conndata*) w;

    if (!conn->size && (revents & EV_READ)) {
        /* Read data. */
        ssize_t r = read(w->fd, conn->buffer, sizeof(conn->buffer));
        fprintf(stderr, "[#%d] Read %zd bytes.\n", w->fd, r);

        if (r <= 0) {
            /* EOF or error: Close connection. */
            const char *errstr = strerror(errno);

            ev_io_stop(EV_A_ w);
            netclose(w->fd, NetdialClose);

            if (r < 0) {
                fprintf(stderr, "[#%d] Closed, error: %s.\n",
                        w->fd, errstr);
            } else {
                fprintf(stderr, "[#%d] Closed, exchanged %zu bytes.\n",
                        w->fd, conn->nbytes);
            }

            free(conn);
            return;
        }

        conn->size = r;
    }

    if (conn->size && (revents & EV_WRITE)) {
        /* Write pending data. */
        ssize_t r = write(w->fd, conn->buffer, conn->size);
        fprintf(stderr, "[#%d] Wrote %zd bytes, %zd pending.\n",
                w->fd, r, conn->size - r);

        if (r <= 0) {
            fprintf(stderr, "[#%d] Write: %s.\n", w->fd, strerror(errno));
            return;
        }

        conn->nbytes += r;
        conn->size -= r;

        if (r < conn->size)
            memmove(conn->buffer, conn->buffer + r, conn->size);
    }
}

static void
handle_accept(EV_P_ ev_io *w, int revents)
{
    char *remote = NULL;
    int nfd = netaccept(w->fd, NetdialNonblock, &remote);
    if (nfd < 0) {
        fprintf(stderr, "Netaccept: %s.\n", strerror(errno));
        return;
    }

    fprintf(stderr, "[#%d] New connection <%s>\n", nfd, remote);
    free(remote);

    struct conndata *conn = calloc(1, sizeof(struct conndata));
    ev_io_init(&conn->watcher, handle_socket_rdwr, nfd, EV_READ | EV_WRITE);
    ev_io_start(EV_A_ &conn->watcher);
}

static void
handle_signal(EV_P_ ev_signal *w, int signum)
{
    fprintf(stderr, "Exiting gracefully...\n");
    ev_break(EV_A_ EVBREAK_ALL);
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

    int fd = netannounce(argv[1], NetdialNonblock, 0);
    if (fd < 0) {
        fprintf(stderr, "Cannot announce %s: %s.\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    ev_io accept_watcher;
    ev_io_init(&accept_watcher, handle_accept, fd, EV_READ);

    ev_signal sigint_watcher;
    ev_signal_init(&sigint_watcher, handle_signal, SIGINT);

    struct ev_loop *loop = EV_DEFAULT;
    ev_io_start(loop, &accept_watcher);
    ev_signal_start(loop, &sigint_watcher);
    ev_run(EV_DEFAULT_ 0);

    netclose(fd, NetdialClose);
    return EXIT_SUCCESS;
}
