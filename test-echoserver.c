/*
 * test-echoserver.c
 * Copyright (C) 2020 Adrian Perez de Castro <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#include "netdial.h"
#include <assert.h>
#include <errno.h>
#include <event.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    Chunksize = 1024U,
};

struct chunk {
    uint8_t       buf[Chunksize];
    size_t        len;
    size_t        off;
    struct chunk *next;
};

struct chunkq {
    struct chunk *head;
    struct chunk *tail;
};

static struct chunk*
mkchunk(void)
{
    struct chunk *c = malloc(sizeof(struct chunk));
    c->len = c->off = 0;
    c->next = NULL;
    return c;
}

static void
freechunk(struct chunk **c)
{
    assert(c);
    free(*c);
    c = NULL;
}

static void
pushchunk(struct chunkq *q, struct chunk *c)
{
    assert(q);
    assert(c);

    if (q->tail) {
        assert(q->tail->next == NULL);
        assert(c->next == NULL);

        q->tail->next = c;
        q->tail = c;
    } else {
        assert(q->head == NULL);

        q->head = q->tail = c;
    }
}

static struct chunk*
popchunk(struct chunkq *q)
{
    assert(q);
    assert(q->head);

    struct chunk *c = q->head;
    q->head = q->head->next;
    return c;
}

static struct chunk*
firstchunk(struct chunkq *q)
{
    assert(q);
    assert(q->head);
    return q->head;
}

static bool
haschunk(const struct chunkq* q)
{
    assert(q);
    return q->head != NULL;
}

struct conn {
    struct event  rev;
    struct event  wev;
    struct chunkq chunks;
    size_t        nbytes;
};

static void
freeconn(struct conn **conn)
{
    assert(conn);

    event_del(&(*conn)->rev);
    while (haschunk(&(*conn)->chunks)) {
        struct chunk *c = popchunk(&(*conn)->chunks);
        freechunk(&c);
    }

    free(*conn);
    conn = NULL;
}

static void
handle_conn_read(int fd, short events, void *data)
{
    struct conn *conn = data;

    for (;;) {
        /* Try to read as many chunks as possible before blocking. */
        fprintf(stderr,
                "[#%d] Attempting to read %u bytes.\n",
                fd, Chunksize);

        struct chunk *c = mkchunk();
        ssize_t r = read(fd, c->buf, Chunksize);
        if (r == 0) {
            /* Client disconnected. */
            fprintf(stderr,
                    "[#%d] Closed, exchanged %zu bytes.\n",
                    fd, conn->nbytes);
            freechunk(&c);
            freeconn(&conn);
            nethangup(fd, NDclose);
            return;
        }

        if (r == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr,
                        "[#%d] Not ready, will read later.\n",
                        fd);
                break;
            }

            fprintf(stderr,
                    "[#%d] Closed, read error: %s.\n",
                    fd, strerror(errno));
            freechunk(&c);
            freeconn(&conn);
            nethangup(fd, NDclose);
            return;
        }

        fprintf(stderr, "[#%d] Read %zd bytes.\n", fd, r);
        c->len = r;
        pushchunk(&conn->chunks, c);

        if (r < Chunksize) {
            fprintf(stderr,
                    "[#%d] Short read, will read later.\n",
                    fd);
            break;
        }
    }

    /* Schedule writing. */
    if (haschunk(&conn->chunks))
        event_add(&conn->wev, NULL);
}

static void
handle_conn_write(int fd, short events, void *data)
{
    struct conn *conn = data;

    while (haschunk(&conn->chunks)) {
        struct chunk *c = firstchunk(&conn->chunks);
        assert(c->len - c->off > 0);
        size_t n = c->len - c->off;

        fprintf(stderr,
                "[#%d] Attempting to write %zu bytes.\n",
                fd, n);

        ssize_t r = write(fd, c->buf + c->off, n);

        if (r == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Try again later. */
                event_add(&conn->wev, NULL);
                return;
            }

            fprintf(stderr,
                    "[#%d] Closed, read error: %s.\n",
                    fd, strerror(errno));
            freechunk(&c);
            freeconn(&conn);
            nethangup(fd, NDclose);
            return;
        }

        fprintf(stderr,
                "[#%d] Wrote %zd bytes, %zd pending.\n",
                fd, r, n - r);

        conn->nbytes += r;

        if (r < n) {
            /* Data pending to write. Update offset and reschedule. */
            c->off += n;
            event_add(&conn->wev, NULL);
            return;
        }

        /* Chunk completely written. */
        popchunk(&conn->chunks);
        freechunk(&c);
    }
}

static void
handle_accept(int fd, short events, void *data)
{
    struct event_base *evbase = data;

    for (unsigned n = 0;; n++) {
        char *remote = NULL;
        int nfd = netaccept(fd, NDdefault, &remote);
        if (nfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "[#%d] Accepted %u new connections.\n", fd, n);
            } else {
                fprintf(stderr, "[#%d] Netaccept: %s.\n", fd, strerror(errno));
            }
            break;
        }

        fprintf(stderr, "[#%d] New connection <%s>\n", nfd, remote);
        free(remote);

        struct conn *conn = calloc(1, sizeof(struct conn));

        event_set(&conn->rev, nfd, EV_READ | EV_PERSIST,
                  handle_conn_read, conn);
        event_base_set(evbase, &conn->rev);

        event_set(&conn->wev, nfd, EV_WRITE,
                  handle_conn_write, conn);
        event_base_set(evbase, &conn->wev);

        /* Start reading only. */
        event_add(&conn->rev, NULL);
    }
}

static void
handle_signal(int signum, short events, void *data)
{
    struct event *ev = data;

    fprintf(stderr, "\rExiting gracefully...\n");
    signal_del(ev);

    event_base_loopexit(ev->ev_base, NULL);
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
    setenv("EVENT_SHOW_METHOD", "1", 1);

    int fd = netannounce(argv[1], NDdefault, 0);
    if (fd < 0) {
        fprintf(stderr, "Cannot announce %s: %s.\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    struct event_base *evbase = event_init();
    fprintf(stderr, "[#%d] Listening on <%s>.\n", fd, argv[1]);

    struct event evaccept;
    event_set(&evaccept, fd, EV_READ | EV_PERSIST, handle_accept, evbase);
    event_base_set(evbase, &evaccept);
    event_add(&evaccept, NULL);

    struct event evsignal;
    signal_set(&evsignal, SIGINT, handle_signal, &evsignal);
    event_base_set(evbase, &evaccept);
    signal_add(&evsignal, NULL);

    event_base_dispatch(evbase);
    event_base_free(evbase);

    nethangup(fd, NDclose);
    return EXIT_SUCCESS;
}
