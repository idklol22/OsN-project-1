#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "resume.h"
#include "jobs.h"

int do_resume(char **argv, int argc)
{
    if (argc < 3) {
        fprintf(stderr, "resume: invalid syntax\n");
        return -1;
    }

    if (argv[1][0] != '%') {
        fprintf(stderr, "resume: invalid syntax\n");
        return -1;
    }

    char *endptr;
    int jid = (int)strtol(argv[1] + 1, &endptr, 10);
    if (*endptr != '\0' || jid <= 0) {
        fprintf(stderr, "resume: invalid syntax\n");
        return -1;
    }

    job_t *job = jobs_get_by_jid(jid);
    if (!job) {
        fprintf(stderr, "resume: no such job\n");
        return -1;
    }

    if (strcmp(argv[2], "bg") == 0) {
        kill(-job->pgid, SIGCONT);
        jobs_mark_running(job->pgid);
        printf("[%d] + Running %s\n", job->jid, job->cmdline[0] ? job->cmdline : job->cmds[0]);
        fflush(stdout);
        return 0;
    } else if (strcmp(argv[2], "fg") == 0) {
        printf("%s\n", job->cmdline[0] ? job->cmdline : job->cmds[0]);
        fflush(stdout);

        tcsetpgrp(STDIN_FILENO, job->pgid);
        kill(-job->pgid, SIGCONT);
        jobs_mark_running(job->pgid);

        int stopped = 0;
        for (int i = 0; i < job->npids; i++) {
            if (job->pids[i] <= 0) continue;
            int st;
            if (waitpid(job->pids[i], &st, WUNTRACED) > 0) {
                if (WIFSTOPPED(st)) {
                    stopped = 1;
                }
            }
        }

        tcsetpgrp(STDIN_FILENO, getpgrp());

        if (stopped) {
            jobs_mark_stopped(job->pgid);
            printf("[%d] + Stopped %s\n", job->jid, job->cmdline[0] ? job->cmdline : job->cmds[0]);
            fflush(stdout);
        }

        return 0;
    } else {
        fprintf(stderr, "resume: invalid syntax\n");
        return -1;
    }
}
