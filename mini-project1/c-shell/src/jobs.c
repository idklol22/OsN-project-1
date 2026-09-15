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

int jobs_add(pid_t pgid, pid_t *pids, char **cmds, int npids, const char *cmdline)
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
    table[slot].state = JOB_RUNNING;
    if (cmdline) {
        strncpy(table[slot].cmdline, cmdline, sizeof(table[slot].cmdline) - 1);
        table[slot].cmdline[sizeof(table[slot].cmdline) - 1] = '\0';
    } else {
        table[slot].cmdline[0] = '\0';
    }

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
    while ((p = waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (!table[i].active) continue;
            for (int j = 0; j < table[i].npids; j++) {
                if (table[i].pids[j] == p) {
                    if (WIFSTOPPED(st)) {
                        table[i].state = JOB_STOPPED;
                    } else if (WIFCONTINUED(st)) {
                        table[i].state = JOB_RUNNING;
                    } else if (WIFEXITED(st) || WIFSIGNALED(st)) {
                        char *cmd = table[i].cmds[j];
                        if (WIFEXITED(st)) {
                            printf("%s with pid %d exited normally\n", cmd, (int)p);
                        } else {
                            printf("%s with pid %d exited abnormally\n", cmd, (int)p);
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
                    }
                    break;
                }
            }
        }
    }
}

int jobs_has_stopped(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table[i].active && table[i].state == JOB_STOPPED) {
            return 1;
        }
    }
    return 0;
}

void jobs_mark_stopped(pid_t pgid)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table[i].active && table[i].pgid == pgid) {
            table[i].state = JOB_STOPPED;
            break;
        }
    }
}

void jobs_mark_running(pid_t pgid)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table[i].active && table[i].pgid == pgid) {
            table[i].state = JOB_RUNNING;
            break;
        }
    }
}

void jobs_send_sighup_all(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table[i].active && table[i].pgid > 0) {
            kill(-table[i].pgid, SIGHUP);
        }
    }
}

job_t *jobs_get_by_jid(int jid)
{
    if (jid <= 0) return NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table[i].active && table[i].jid == jid) {
            return &table[i];
        }
    }
    return NULL;
}

void jobs_remove(pid_t pgid)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table[i].active && table[i].pgid == pgid) {
            table[i].active = 0;
            njobs--;
            break;
        }
    }
}

int jobs_contains_pid(pid_t pid)
{
    if (pid <= 0) return 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!table[i].active) continue;
        for (int j = 0; j < table[i].npids; j++) {
            if (table[i].pids[j] == pid) return 1;
        }
    }
    return 0;
}

static int cmp_jobs(const void *a, const void *b)
{
    const job_t *ja = *(const job_t **)a;
    const job_t *jb = *(const job_t **)b;
    return ja->jid - jb->jid;
}

void jobs_print_activities(void)
{
    job_t *active_jobs[MAX_JOBS];
    int count = 0;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (!table[i].active) continue;

        int alive = 0;
        for (int j = 0; j < table[i].npids; j++) {
            if (table[i].pids[j] > 0) { alive = 1; break; }
        }
        if (alive) {
            active_jobs[count++] = &table[i];
        }
    }

    qsort(active_jobs, count, sizeof(job_t *), cmp_jobs);

    for (int k = 0; k < count; k++) {
        job_t *job = active_jobs[k];
        printf("[%d] pgid %d\n", job->jid, (int)job->pgid);
        for (int j = 0; j < job->npids; j++) {
            pid_t p = job->pids[j];
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
            const char *state = (state_ch == 'T' || job->state == JOB_STOPPED) ? "Stopped" : "Running";
            printf("  %d %s %s\n", (int)p, job->cmds[j], state);
        }
    }
    fflush(stdout);
}
