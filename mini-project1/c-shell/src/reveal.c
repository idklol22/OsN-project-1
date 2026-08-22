#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include "reveal.h"
#include "hop.h"

static int cmp_ent(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}


static void do_tree_rooted(const char *base, const char *prefix, int show_hidden)
{
    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *de;
    char **names = NULL;
    int cnt = 0, cap = 0;

    while ((de = readdir(d))) {
        char *n = de->d_name;
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        if (!show_hidden && n[0] == '.') continue;
        if (cnt == cap) {
            cap = cap ? cap * 2 : 16;
            names = realloc(names, (size_t)cap * sizeof(char *));
        }
        names[cnt++] = strdup(n);
    }
    closedir(d);

    qsort(names, (size_t)cnt, sizeof(char *), cmp_ent);

    for (int i = 0; i < cnt; i++) {
        char full[PATH_MAX];
        char rel[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", base, names[i]);
        if (prefix[0])
            snprintf(rel, sizeof(rel), "%s/%s", prefix, names[i]);
        else
            snprintf(rel, sizeof(rel), "%s", names[i]);

        struct stat st;
        if (stat(full, &st) != 0) { free(names[i]); continue; }

        if (S_ISDIR(st.st_mode)) {
            printf("%s/\n", rel);
            do_tree_rooted(full, rel, show_hidden);
        } else {
            printf("%s\n", rel);
        }
        free(names[i]);
    }
    free(names);
}

static void do_flat(const char *base, int show_hidden)
{
    DIR *d = opendir(base);
    if (!d) return;

    struct dirent *de;
    char **names = NULL;
    int cnt = 0, cap = 0;

    while ((de = readdir(d))) {
        char *n = de->d_name;
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        if (!show_hidden && n[0] == '.') continue;
        if (cnt == cap) {
            cap = cap ? cap * 2 : 16;
            names = realloc(names, (size_t)cap * sizeof(char *));
        }
        names[cnt++] = strdup(n);
    }
    closedir(d);

    qsort(names, (size_t)cnt, sizeof(char *), cmp_ent);

    for (int i = 0; i < cnt; i++) {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", base, names[i]);
        struct stat st;
        int is_dir = 0;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = 1;
        if (is_dir)
            printf("%s/\n", names[i]);
        else
            printf("%s\n", names[i]);
        free(names[i]);
    }
    free(names);
}

int do_reveal(char **argv, int argc)
{
    int flag_a = 0, flag_t = 0;
    char *target = NULL;
    int n_targets = 0;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] != '\0') {
            int j = 1;
            while (arg[j]) {
                if (arg[j] == 'a') flag_a = 1;
                else if (arg[j] == 't') flag_t = 1;
                else { fprintf(stderr, "reveal: invalid syntax\n"); return -1; }
                j++;
            }
        } else {
            n_targets++;
            if (n_targets > 1) {
                fprintf(stderr, "reveal: invalid syntax\n");
                return -1;
            }
            target = arg;
        }
    }

    char resolved[PATH_MAX];

    if (!target) {
        if (!getcwd(resolved, sizeof(resolved))) {
            fprintf(stderr, "reveal: no such directory\n");
            return -1;
        }
    } else if (target[0] == '~' && (target[1] == '\0' || target[1] == '/')) {
        extern char homedir[PATH_MAX];
        snprintf(resolved, sizeof(resolved), "%s%s", homedir, target + 1);
    } else if (strcmp(target, ".") == 0) {
        if (!getcwd(resolved, sizeof(resolved))) {
            fprintf(stderr, "reveal: no such directory\n"); return -1;
        }
    } else if (strcmp(target, "..") == 0) {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) { fprintf(stderr, "reveal: no such directory\n"); return -1; }
        char *sl = strrchr(cwd, '/');
        if (sl && sl != cwd) *sl = '\0';
        else if (sl == cwd) { cwd[1] = '\0'; }
        strncpy(resolved, cwd, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
    } else if (strcmp(target, "-") == 0) {
        if (prevdir[0] == '\0') {
            fprintf(stderr, "reveal: no such directory\n"); return -1;
        }
        strncpy(resolved, prevdir, PATH_MAX - 1);
        resolved[PATH_MAX - 1] = '\0';
    } else {
        struct stat st;
        if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
            char *r = realpath(target, resolved);
            if (!r) { fprintf(stderr, "reveal: no such directory\n"); return -1; }
        } else {
            fprintf(stderr, "reveal: no such directory\n"); return -1;
        }
    }

    struct stat chk;
    if (stat(resolved, &chk) != 0 || !S_ISDIR(chk.st_mode)) {
        fprintf(stderr, "reveal: no such directory\n");
        return -1;
    }

    if (flag_t) {
        do_tree_rooted(resolved, "", flag_a);
    } else {
        do_flat(resolved, flag_a);
    }

    return 0;
}
