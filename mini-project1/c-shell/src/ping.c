#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ping.h"

int do_ping(char **argv, int argc)
{
    if (argc != 3) {
        fprintf(stderr, "ping: invalid syntax\n");
        return -1;
    }

    char *endptr;
    long sig = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || sig < 0 || argv[2][0] == '\0' || argv[2][0] == '-') {
        fprintf(stderr, "ping: invalid syntax\n");
        return -1;
    }

    /* 25% Partial implementation stub: Basic validation complete */
    fprintf(stderr, "ping: no such process found\n");
    return -1;
}
