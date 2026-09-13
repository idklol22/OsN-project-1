#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include "lexer.h"
#include "parser.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include "locate.h"
#include "exec.h"
#include "jobs.h"

char homedir[PATH_MAX];

static void print_prompt(void)
{
    char cwd[PATH_MAX];
    char uname[256], hname[256];

    if (getenv("USER"))
        strncpy(uname, getenv("USER"), sizeof(uname) - 1);
    else
        strncpy(uname, "user", sizeof(uname) - 1);
    uname[sizeof(uname) - 1] = '\0';

    if (gethostname(hname, sizeof(hname)) != 0)
        strncpy(hname, "host", sizeof(hname) - 1);
    hname[sizeof(hname) - 1] = '\0';

    if (!getcwd(cwd, sizeof(cwd)))
        strncpy(cwd, "?", sizeof(cwd) - 1);

    size_t hlen = strlen(homedir);
    char *disp;
    char dispbuf[PATH_MAX + 2];

    if (strncmp(cwd, homedir, hlen) == 0 &&
        (cwd[hlen] == '/' || cwd[hlen] == '\0')) {
        dispbuf[0] = '~';
        strncpy(dispbuf + 1, cwd + hlen, sizeof(dispbuf) - 2);
        dispbuf[sizeof(dispbuf) - 1] = '\0';
        disp = dispbuf;
    } else {
        disp = cwd;
    }

    printf("<%s@%s:%s> ", uname, hname, disp);
    fflush(stdout);
}

static void sigint_handler(int sig)
{
    (void)sig;
    printf("\n");
    print_prompt();
    fflush(stdout);
}

static void sigtstp_handler(int sig)
{
    (void)sig;
    printf("\n");
    print_prompt();
    fflush(stdout);
}

static void sigchld_handler(int sig)
{
    (void)sig;
    jobs_reap();
}

int main(void)
{
    if (!getcwd(homedir, sizeof(homedir))) {
        perror("getcwd");
        return 1;
    }

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    pid_t shell_pid = getpid();
    setpgid(shell_pid, shell_pid);
    tcsetpgrp(STDIN_FILENO, shell_pid);

    hop_init(homedir);
    jobs_init();

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction sa_int;
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_tstp;
    sa_tstp.sa_handler = sigtstp_handler;
    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa_tstp, NULL);

    char line[shell_inmax];
    int eof_warned = 0;

    while (1) {
        print_prompt();

        if (!fgets(line, sizeof(line), stdin)) {
            if (jobs_has_stopped() && !eof_warned) {
                printf("cshell: there are stopped jobs\n");
                eof_warned = 1;
                continue;
            }
            printf("\n");
            jobs_send_sighup_all();
            break;
        }

        eof_warned = 0;

        size_t ln = strlen(line);
        if (ln > 0 && line[ln - 1] == '\n')
            line[ln - 1] = '\0';

        tok_list tl;
        if (lex(line, &tl) < 0) {
            continue;
        }

        cmd *cmds = parse(&tl);
        tl_free(&tl);
        if (!cmds) continue;

        do_exec(cmds);

        cmds_free(cmds);
    }

    return 0;
}
