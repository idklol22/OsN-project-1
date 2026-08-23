#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include "locate.h"

static int check_exec(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (!S_ISREG(st.st_mode)) return 0;
    if (access(path, X_OK) != 0) return 0;
    return 1;
}

int do_locate(char **argv, int argc)
{
    if (argc <= 1) {
        fprintf(stderr, "locate: invalid syntax\n");
        return -1;
    }

    char *pathenv = getenv("PATH");

    for (int i = 1; i < argc; i++) {
        char *name = argv[i];
        int found = 0;

        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            char full[PATH_MAX * 2];
            snprintf(full, sizeof(full), "%s/%s", cwd, name);
            if (check_exec(full)) {
                printf("%s\n", full);
                found = 1;
            }
        }

        if (pathenv) {
            char *pcopy = strdup(pathenv);
            char *tok = strtok(pcopy, ":");
            while (tok) {
                char full[PATH_MAX * 2];
                snprintf(full, sizeof(full), "%s/%s", tok, name);
                if (check_exec(full)) {
                    printf("%s\n", full);
                    found = 1;
                }
                tok = strtok(NULL, ":");
            }
            free(pcopy);
        }

        if (!found)
            fprintf(stderr, "locate: command not found (%s)\n", name);
    }

    return 0;
}
