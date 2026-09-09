#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "jobs.h"

static job_t table[MAX_JOBS];
static int ntotal = 0;
static int njobs = 0;

void jobs_init(void)
{
    memset(table, 0, sizeof(table));
    ntotal = 0;
    njobs = 0;
}

int jobs_add(pid_t pgid, pid_t *pids, char **cmds, int npids)
{
    if (njobs >= MAX_JOBS) return -1;
    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!table[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    ntotal++;
    table[slot].jid = ntotal;
    table[slot].pgid = pgid;
    table[slot].npids = npids < MAX_PIDS_PER_JOB ? npids : MAX_PIDS_PER_JOB;
    table[slot].active = 1;
    for (int i = 0; i < table[slot].npids; i++) {
        table[slot].pids[i] = pids[i];
        strncpy(table[slot].cmds[i], cmds[i], 255);
        table[slot].cmds[i][255] = '\0';
    }
    njobs++;
    return table[slot].jid;
}

void jobs_reap(void)
{
    int st;
    pid_t p;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (!table[i].active) continue;
            for (int j = 0; j < table[i].npids; j++) {
                if (table[i].pids[j] == p) {
                    char *cmd = table[i].cmds[j];
                    if (WIFEXITED(st)) {
                        printf("\n%s with pid %d exited normally\n", cmd, (int)p);
                    } else {
                        printf("\n%s with pid %d exited abnormally\n", cmd, (int)p);
                    }
                    fflush(stdout);
                    table[i].pids[j] = -1;

                    int all_done = 1;
                    for (int k = 0; k < table[i].npids; k++) {
                        if (table[i].pids[k] > 0) { all_done = 0; break; }
                    }
                    if (all_done) {
                        table[i].active = 0;
                        njobs--;
                    }
                    break;
                }
            }
        }
    }
}

void jobs_print_activities(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!table[i].active) continue;

        int alive = 0;
        for (int j = 0; j < table[i].npids; j++) {
            if (table[i].pids[j] > 0) { alive = 1; break; }
        }
        if (!alive) continue;

        printf("[%d] pgid %d\n", table[i].jid, (int)table[i].pgid);
        for (int j = 0; j < table[i].npids; j++) {
            pid_t p = table[i].pids[j];
            if (p <= 0) continue;

            char state_ch = '?';
            char statpath[64];
            snprintf(statpath, sizeof(statpath), "/proc/%d/stat", (int)p);
            FILE *f = fopen(statpath, "r");
            if (f) {
                int dummy_pid;
                char comm[256];
                char st_c;
                if (fscanf(f, "%d %255s %c", &dummy_pid, comm, &st_c) == 3)
                    state_ch = st_c;
                fclose(f);
            }
            const char *state = (state_ch == 'T') ? "Stopped" : "Running";
            printf("  %d %s  %s\n", (int)p, table[i].cmds[j], state);
        }
    }
}
