#ifndef jobs_h
#define jobs_h

#include <sys/types.h>

#define MAX_JOBS 256
#define MAX_PIDS_PER_JOB 64

typedef struct {
    int jid;
    pid_t pgid;
    pid_t pids[MAX_PIDS_PER_JOB];
    char cmds[MAX_PIDS_PER_JOB][256];
    int npids;
    int active;
} job_t;

void jobs_init(void);
int jobs_add(pid_t pgid, pid_t *pids, char **cmds, int npids);
void jobs_reap(void);
void jobs_print_activities(void);

#endif
