/*
 * test-echoclient.c
 * Copyright (C) 2020 Adrian Perez de Castro <aperez@igalia.com>
 *
 * Distributed under terms of the MIT license.
 */

#include "netdial.h"
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <address>\n", argv[0]);
        fprintf(stderr, "Example: %s tcp:localhost:echo\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = netdial(argv[1], NDblocking);
    if (fd == -1) {
        fprintf(stderr, "Cannot dial %s: %s.\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    for (;;) {
        char buffer[512];
        ssize_t ret;

        if ((ret = read(STDIN_FILENO, buffer, sizeof(buffer))) < 0) {
            fprintf(stderr, "Read error: %s.\n", strerror(errno));
            break;
        }
        if (ret == 0)
            break;

        if ((ret = write(fd, buffer, ret)) < 0) {
            fprintf(stderr, "Socket write error: %s.\n", strerror(errno));
            break;
        }

        if ((ret = read(fd, buffer, sizeof(buffer))) < 0) {
            fprintf(stderr, "Socket read error: %s.\n", strerror(errno));
            break;
        }

        if ((ret = write(STDOUT_FILENO, buffer, ret)) < 0) {
            fprintf(stderr, "Write error: %s.\n", strerror(errno));
            break;
        }
    }

    netclose(fd, NDclose);
    return EXIT_SUCCESS;
}
