#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "resume.h"
#include "jobs.h"

static volatile sig_atomic_t g_timed_out = 0;

static void sigalrm_handler(int sig)
{
    (void)sig;
    g_timed_out = 1;
}

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
        if (argc != 3) {
            fprintf(stderr, "resume: invalid syntax\n");
            return -1;
        }
        kill(-job->pgid, SIGCONT);
        jobs_mark_running(job->pgid);
        printf("[%d] + Running %s\n", job->jid, job->cmdline[0] ? job->cmdline : job->cmds[0]);
        fflush(stdout);
        return 0;
    } else if (strcmp(argv[2], "fg") == 0) {
        int timeout_sec = 0;
        if (argc == 5) {
            if (strcmp(argv[3], "--timeout") != 0) {
                fprintf(stderr, "resume: invalid syntax\n");
                return -1;
            }
            timeout_sec = (int)strtol(argv[4], &endptr, 10);
            if (*endptr != '\0' || timeout_sec <= 0) {
                fprintf(stderr, "resume: invalid syntax\n");
                return -1;
            }
        } else if (argc != 3) {
            fprintf(stderr, "resume: invalid syntax\n");
            return -1;
        }

        printf("%s\n", job->cmdline[0] ? job->cmdline : job->cmds[0]);
        fflush(stdout);

        tcsetpgrp(STDIN_FILENO, job->pgid);
        kill(-job->pgid, SIGCONT);
        jobs_mark_running(job->pgid);

        g_timed_out = 0;
        struct sigaction sa, old_sa;
        if (timeout_sec > 0) {
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = sigalrm_handler;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGALRM, &sa, &old_sa);
            alarm((unsigned int)timeout_sec);
        }

        int stopped = 0;
        for (int i = 0; i < job->npids; i++) {
            if (job->pids[i] <= 0) continue;
            int st;
            if (waitpid(job->pids[i], &st, WUNTRACED) > 0) {
                if (WIFSTOPPED(st)) {
                    stopped = 1;
                } else if (WIFEXITED(st) || WIFSIGNALED(st)) {
                    job->pids[i] = -1;
                }
            }
        }

        if (timeout_sec > 0) {
            alarm(0);
            sigaction(SIGALRM, &old_sa, NULL);
        }

        tcsetpgrp(STDIN_FILENO, getpgrp());

        if (g_timed_out) {
            kill(-job->pgid, SIGTERM);
            fprintf(stderr, "resume: job timed out\n");
            jobs_remove(job->pgid);
        } else if (stopped) {
            jobs_mark_stopped(job->pgid);
            printf("[%d] + Stopped %s\n", job->jid, job->cmdline[0] ? job->cmdline : job->cmds[0]);
            fflush(stdout);
        } else {
            int all_done = 1;
            for (int i = 0; i < job->npids; i++) {
                if (job->pids[i] > 0) { all_done = 0; break; }
            }
            if (all_done) {
                jobs_remove(job->pgid);
            }
        }

        return 0;
    } else {
        fprintf(stderr, "resume: invalid syntax\n");
        return -1;
    }
}
