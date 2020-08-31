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
#include <unistd.h>

int
main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <address>\n", argv[0]);
        fprintf(stderr, "Example: %s tcp:localhost:echo\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = netdial(argv[1], NetdialCloexec);
    if (fd == -1) {
        fprintf(stderr, "Cannot dial %s: %s.\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    close(fd);
}
