#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include "lexer.h"
#include "parser.h"

static char homedir[PATH_MAX];

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

int main(void)
{
    if (!getcwd(homedir, sizeof(homedir))) {
        perror("getcwd");
        return 1;
    }

    char line[shell_inmax];

    while (1) {
        print_prompt();

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        size_t ln = strlen(line);
        if (ln > 0 && line[ln - 1] == '\n')
            line[ln - 1] = '\0';

        tok_list tl;
        if (lex(line, &tl) < 0) {
            continue;
        }

        cmd *cmds = parse(&tl);
        tl_free(&tl);
        if (cmds)
            cmds_free(cmds);
    }

    return 0;
}
