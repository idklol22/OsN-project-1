#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <ctype.h>
#include "ping.h"
#include "jobs.h"

static int is_nonneg_int(const char *s)
{
    if (!s || *s == '\0') return 0;
    for (int i = 0; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i])) return 0;
    }
    return 1;
}

int do_ping(char **argv, int argc)
{
    if (argc != 3) {
        fprintf(stderr, "ping: invalid syntax\n");
        return -1;
    }

    // Validate signal_number before <target> is looked up
    if (!is_nonneg_int(argv[2])) {
        fprintf(stderr, "ping: invalid syntax\n");
        return -1;
    }

    int actual_sig = 0;
    for (int i = 0; argv[2][i] != '\0'; i++) {
        actual_sig = (actual_sig * 10 + (argv[2][i] - '0')) % 64;
    }

    if (argv[1][0] == '%') {
        if (!is_nonneg_int(argv[1] + 1)) {
            fprintf(stderr, "ping: invalid syntax\n");
            return -1;
        }
        char *endptr;
        long jid = strtol(argv[1] + 1, &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "ping: invalid syntax\n");
            return -1;
        }
        job_t *job = jobs_get_by_jid((int)jid);
        if (!job) {
            fprintf(stderr, "ping: no such process found\n");
            return -1;
        }
        if (kill(-job->pgid, actual_sig) < 0) {
            for (int i = 0; i < job->npids; i++) {
                if (job->pids[i] > 0) {
                    kill(job->pids[i], actual_sig);
                }
            }
        }
        printf("Sent signal %s to %s\n", argv[2], argv[1]);
        fflush(stdout);
        return 0;
    } else {
        if (!is_nonneg_int(argv[1])) {
            fprintf(stderr, "ping: invalid syntax\n");
            return -1;
        }
        char *endptr;
        long target_pid = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "ping: invalid syntax\n");
            return -1;
        }
        if (!jobs_contains_pid((pid_t)target_pid)) {
            fprintf(stderr, "ping: no such process found\n");
            return -1;
        }
        kill((pid_t)target_pid, actual_sig);
        printf("Sent signal %s to %s\n", argv[2], argv[1]);
        fflush(stdout);
        return 0;
    }
}
