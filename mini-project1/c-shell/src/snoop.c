#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include "snoop.h"
#include "exec.h"

typedef struct {
    long nr;
    int calls;
    double total_time;
    int first_seen;
} syscall_info_t;

#define MAX_SYSCALLS 1024
static syscall_info_t stats[MAX_SYSCALLS];
static int total_stats = 0;
static int seen_order = 0;

static void reset_stats(void)
{
    memset(stats, 0, sizeof(stats));
    total_stats = 0;
    seen_order = 0;
}

static void add_syscall_time(long nr, double duration)
{
    for (int i = 0; i < total_stats; i++) {
        if (stats[i].nr == nr) {
            stats[i].calls++;
            stats[i].total_time += duration;
            return;
        }
    }
    if (total_stats < MAX_SYSCALLS) {
        stats[total_stats].nr = nr;
        stats[total_stats].calls = 1;
        stats[total_stats].total_time = duration;
        stats[total_stats].first_seen = seen_order++;
        total_stats++;
    }
}

static int compare_stats(const void *p1, const void *p2)
{
    const syscall_info_t *a = (const syscall_info_t *)p1;
    const syscall_info_t *b = (const syscall_info_t *)p2;

    if (b->calls != a->calls) {
        return b->calls - a->calls;
    }
    return a->first_seen - b->first_seen;
}

static const char *lookup_name(long nr, char *fallback, size_t sz)
{
    static const char *names[] = {
        [0] = "read", [1] = "write", [2] = "open", [3] = "close",
        [4] = "stat", [5] = "fstat", [6] = "lstat", [7] = "poll",
        [8] = "lseek", [9] = "mmap", [10] = "mprotect", [11] = "munmap",
        [12] = "brk", [13] = "rt_sigaction", [14] = "rt_sigprocmask",
        [15] = "rt_sigreturn", [16] = "ioctl", [17] = "pread64",
        [18] = "pwrite64", [19] = "readv", [20] = "writev",
        [21] = "access", [22] = "pipe", [23] = "select",
        [24] = "sched_yield", [25] = "mremap", [26] = "msync",
        [27] = "mincore", [28] = "madvise", [29] = "shmget",
        [30] = "shmat", [31] = "shmctl", [32] = "dup",
        [33] = "dup2", [34] = "pause", [35] = "nanosleep",
        [36] = "getitimer", [37] = "alarm", [38] = "setitimer",
        [39] = "getpid", [40] = "sendfile", [41] = "socket",
        [42] = "connect", [43] = "accept", [44] = "sendto",
        [45] = "recvfrom", [46] = "sendmsg", [47] = "recvmsg",
        [48] = "shutdown", [49] = "bind", [50] = "listen",
        [51] = "getsockname", [52] = "getpeername", [53] = "socketpair",
        [54] = "setsockopt", [55] = "getsockopt", [56] = "clone",
        [57] = "fork", [58] = "vfork", [59] = "execve",
        [60] = "exit", [61] = "wait4", [62] = "kill",
        [63] = "uname", [64] = "semget", [65] = "semop",
        [66] = "semctl", [67] = "shmdt", [68] = "msgget",
        [69] = "msgsnd", [70] = "msgrcv", [71] = "msgctl",
        [72] = "fcntl", [73] = "flock", [74] = "fsync",
        [75] = "fdatasync", [76] = "truncate", [77] = "ftruncate",
        [78] = "getdents", [79] = "getcwd", [80] = "chdir",
        [81] = "fchdir", [82] = "rename", [83] = "mkdir",
        [84] = "rmdir", [85] = "creat", [86] = "link",
        [87] = "unlink", [88] = "symlink", [89] = "readlink",
        [90] = "chmod", [91] = "fchmod", [92] = "chown",
        [93] = "fchown", [94] = "lchown", [95] = "umask",
        [96] = "gettimeofday", [97] = "getrlimit", [98] = "getrusage",
        [99] = "sysinfo", [100] = "times", [101] = "ptrace",
        [102] = "getuid", [103] = "syslog", [104] = "getgid",
        [105] = "setuid", [106] = "setgid", [107] = "geteuid",
        [108] = "getegid", [109] = "setpgid", [110] = "getppid",
        [111] = "getpgrp", [112] = "setsid", [113] = "setreuid",
        [114] = "setregid", [115] = "getgroups", [116] = "setgroups",
        [117] = "setresuid", [118] = "getresuid", [119] = "setresgid",
        [120] = "getresgid", [121] = "getpgid", [122] = "setfsuid",
        [123] = "setfsgid", [124] = "getsid", [125] = "capget",
        [126] = "capset", [127] = "rt_sigpending", [128] = "rt_sigtimedwait",
        [129] = "rt_sigqueueinfo", [130] = "rt_sigsuspend", [131] = "sigaltstack",
        [132] = "utime", [133] = "mknod", [134] = "uselib",
        [135] = "personality", [136] = "ustat", [137] = "statfs",
        [138] = "fstatfs", [139] = "sysfs", [140] = "getpriority",
        [141] = "setpriority", [142] = "sched_setparam", [143] = "sched_getparam",
        [144] = "sched_setscheduler", [145] = "sched_getscheduler",
        [146] = "sched_get_priority_max", [147] = "sched_get_priority_min",
        [148] = "sched_rr_get_interval", [149] = "mlock", [150] = "munlock",
        [151] = "mlockall", [152] = "munlockall", [153] = "vhangup",
        [154] = "modify_ldt", [155] = "pivot_root", [156] = "_sysctl",
        [157] = "prctl", [158] = "arch_prctl", [159] = "adjtimex",
        [160] = "setrlimit", [161] = "chroot", [162] = "sync",
        [163] = "acct", [164] = "settimeofday", [165] = "mount",
        [166] = "umount2", [167] = "swapon", [168] = "swapoff",
        [169] = "reboot", [170] = "sethostname", [171] = "setdomainname",
        [172] = "iopl", [173] = "ioperm", [174] = "create_module",
        [175] = "init_module", [176] = "delete_module", [177] = "get_kernel_syms",
        [178] = "query_module", [179] = "quotactl", [180] = "nfsservctl",
        [186] = "gettid", [187] = "readahead", [188] = "setxattr",
        [189] = "lsetxattr", [190] = "fsetxattr", [191] = "getxattr",
        [192] = "lgetxattr", [193] = "fgetxattr", [194] = "listxattr",
        [195] = "llistxattr", [196] = "flistxattr", [197] = "removexattr",
        [198] = "lremovexattr", [199] = "fremovexattr", [200] = "tkill",
        [201] = "time", [202] = "futex", [203] = "sched_setaffinity",
        [204] = "sched_getaffinity", [205] = "set_thread_area", [206] = "io_setup",
        [207] = "io_destroy", [208] = "io_getevents", [209] = "io_submit",
        [210] = "io_cancel", [211] = "get_thread_area", [212] = "lookup_dcookie",
        [213] = "epoll_create", [216] = "remap_file_pages", [217] = "getdents64",
        [218] = "set_tid_address", [219] = "restart_syscall", [220] = "semtimedop",
        [221] = "fadvise64", [222] = "timer_create", [223] = "timer_settime",
        [224] = "timer_gettime", [225] = "timer_getoverrun", [226] = "timer_delete",
        [227] = "clock_settime", [228] = "clock_gettime", [229] = "clock_getres",
        [230] = "clock_nanosleep", [231] = "exit_group", [232] = "epoll_wait",
        [233] = "epoll_ctl", [234] = "tgkill", [235] = "utimes",
        [257] = "openat", [258] = "mkdirat", [259] = "mknodat",
        [260] = "fchownat", [261] = "futimesat", [262] = "newfstatat",
        [263] = "unlinkat", [264] = "renameat", [265] = "linkat",
        [266] = "symlinkat", [267] = "readlinkat", [268] = "fchmodat",
        [269] = "faccessat", [270] = "pselect6", [271] = "ppoll",
        [272] = "unshare", [273] = "set_robust_list", [274] = "get_robust_list",
        [275] = "splice", [276] = "tee", [277] = "sync_file_range",
        [278] = "vmsplice", [279] = "move_pages", [280] = "utimensat",
        [281] = "epoll_pwait", [282] = "signalfd", [283] = "timerfd_create",
        [284] = "eventfd", [285] = "fallocate", [286] = "timerfd_settime",
        [287] = "timerfd_gettime", [288] = "accept4", [289] = "signalfd4",
        [290] = "eventfd2", [291] = "epoll_create1", [292] = "dup3",
        [293] = "pipe2", [294] = "inotify_init1", [295] = "preadv",
        [296] = "pwritev", [302] = "prlimit64", [318] = "getrandom",
        [332] = "statx", [334] = "rseq"
    };

    if (nr >= 0 && nr < (long)(sizeof(names) / sizeof(names[0])) && names[nr] != NULL) {
        return names[nr];
    }
    snprintf(fallback, sz, "syscall_%ld", nr);
    return fallback;
}

static void trace_loop(pid_t target_pid)
{
    int in_syscall = 0;
    long active_nr = -1;
    struct timespec enter_time;

    ptrace(PTRACE_SYSCALL, target_pid, 0, 0);

    while (1) {
        int status = 0;
        pid_t w = waitpid(target_pid, &status, 0);
        if (w < 0) break;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (in_syscall && active_nr >= 0) {
                struct timespec leave_time;
                clock_gettime(CLOCK_MONOTONIC, &leave_time);
                double elapsed = (double)(leave_time.tv_sec - enter_time.tv_sec) +
                                 (double)(leave_time.tv_nsec - enter_time.tv_nsec) / 1e9;
                if (elapsed < 0) elapsed = 0;
                add_syscall_time(active_nr, elapsed);
            }
            break;
        }

        if (WIFSTOPPED(status)) {
            int sig = WSTOPSIG(status);
            if (sig == (SIGTRAP | 0x80)) {
                struct user_regs_struct regs;
                if (ptrace(PTRACE_GETREGS, target_pid, NULL, &regs) == 0) {
                    long nr = (long)regs.orig_rax;
                    if (!in_syscall) {
                        in_syscall = 1;
                        active_nr = nr;
                        clock_gettime(CLOCK_MONOTONIC, &enter_time);
                    } else {
                        struct timespec leave_time;
                        clock_gettime(CLOCK_MONOTONIC, &leave_time);
                        double elapsed = (double)(leave_time.tv_sec - enter_time.tv_sec) +
                                         (double)(leave_time.tv_nsec - enter_time.tv_nsec) / 1e9;
                        if (elapsed < 0) elapsed = 0;
                        add_syscall_time(active_nr, elapsed);
                        in_syscall = 0;
                        active_nr = -1;
                    }
                }
                ptrace(PTRACE_SYSCALL, target_pid, 0, 0);
            } else {
                int pass = (sig == SIGSTOP) ? 0 : sig;
                ptrace(PTRACE_SYSCALL, target_pid, 0, pass);
            }
        }
    }

    // Print summary
    qsort(stats, total_stats, sizeof(syscall_info_t), compare_stats);

    printf("%-14s%-8s%s\n", "syscall", "calls", "time");
    char namebuf[32];
    for (int i = 0; i < total_stats; i++) {
        const char *name = lookup_name(stats[i].nr, namebuf, sizeof(namebuf));
        int pad = 14 - (int)strlen(name);
        if (pad < 1) pad = 1;
        printf("%s%*s%-8d%.3fs\n", name, pad, "", stats[i].calls, stats[i].total_time);
    }
    fflush(stdout);
}

int do_snoop(char **argv, int argc)
{
    if (argc < 2) {
        fprintf(stderr, "snoop: invalid syntax\n");
        return -1;
    }

    reset_stats();

    if (strcmp(argv[1], "-p") == 0) {
        if (argc != 3) {
            fprintf(stderr, "snoop: invalid syntax\n");
            return -1;
        }
        for (int i = 0; argv[2][i]; i++) {
            if (!isdigit((unsigned char)argv[2][i])) {
                fprintf(stderr, "snoop: no such process\n");
                return -1;
            }
        }
        long val = strtol(argv[2], NULL, 10);
        if (val <= 0) {
            fprintf(stderr, "snoop: no such process\n");
            return -1;
        }
        pid_t pid = (pid_t)val;

        if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
            fprintf(stderr, "snoop: no such process\n");
            return -1;
        }

        int status;
        waitpid(pid, &status, 0);
        ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);

        trace_loop(pid);
        return 0;
    } else {
        char runpath[2048];
        if (!find_exec(argv[1], runpath, sizeof(runpath))) {
            fprintf(stderr, "snoop: command not found\n");
            return -1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return -1;
        }

        if (pid == 0) {
            ptrace(PTRACE_TRACEME, 0, NULL, NULL);
            execv(runpath, &argv[1]);
            perror("execv");
            _exit(1);
        }

        int status;
        waitpid(pid, &status, 0);
        ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);

        trace_loop(pid);
        return 0;
    }
}
