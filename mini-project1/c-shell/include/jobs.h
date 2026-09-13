#ifndef jobs_h
#define jobs_h

#include <sys/types.h>

#define MAX_JOBS 256
#define MAX_PIDS_PER_JOB 64

#define JOB_RUNNING 0
#define JOB_STOPPED 1

typedef struct {
    int jid;
    pid_t pgid;
    pid_t pids[MAX_PIDS_PER_JOB];
    char cmds[MAX_PIDS_PER_JOB][256];
    int npids;
    int active;
    int state;
    char cmdline[1024];
} job_t;

void jobs_init(void);
int jobs_add(pid_t pgid, pid_t *pids, char **cmds, int npids, const char *cmdline);
void jobs_reap(void);
void jobs_print_activities(void);
int jobs_has_stopped(void);
void jobs_mark_stopped(pid_t pgid);
void jobs_mark_running(pid_t pgid);
void jobs_send_sighup_all(void);
job_t *jobs_get_by_jid(int jid);
void jobs_remove(pid_t pgid);

#endif
