#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "peek.h"

#define CHUNK 4096

typedef struct {
    char **lines;
    int *lnums;
    int n, cap;
} linebuf;

static void lb_push(linebuf *lb, char *s, int lnum)
{
    if (lb->n == lb->cap) {
        lb->cap = lb->cap ? lb->cap * 2 : 64;
        lb->lines = realloc(lb->lines, (size_t)lb->cap * sizeof(char *));
        lb->lnums = realloc(lb->lnums, (size_t)lb->cap * sizeof(int));
    }
    lb->lines[lb->n] = s;
    lb->lnums[lb->n] = lnum;
    lb->n++;
}

static void lb_free(linebuf *lb)
{
    for (int i = 0; i < lb->n; i++) free(lb->lines[i]);
    free(lb->lines);
    free(lb->lnums);
    lb->lines = NULL; lb->lnums = NULL;
    lb->n = lb->cap = 0;
}

static void print_lines(linebuf *lb, int flag_n)
{
    for (int i = 0; i < lb->n; i++) {
        char *l = lb->lines[i];
        if (flag_n) {
            if (l[0] != '\0')
                printf("%d %s\n", lb->lnums[i], l);
            else
                printf("\n");
        } else {
            printf("%s\n", l);
        }
    }
}

static void reverse_lb(linebuf *lb)
{
    int lo = 0, hi = lb->n - 1;
    while (lo < hi) {
        char *tmp = lb->lines[lo]; lb->lines[lo] = lb->lines[hi]; lb->lines[hi] = tmp;
        int ti = lb->lnums[lo]; lb->lnums[lo] = lb->lnums[hi]; lb->lnums[hi] = ti;
        lo++; hi--;
    }
}

static void read_forward(int fd, linebuf *lb)
{
    char buf[CHUNK];
    char *cur = NULL;
    int clen = 0, ccap = 0;
    int lineno = 1;

    ssize_t n;
    while ((n = read(fd, buf, CHUNK)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                if (clen + 1 > ccap) {
                    ccap = ccap ? ccap * 2 : 64;
                    cur = realloc(cur, (size_t)ccap);
                }
                cur[clen] = '\0';
                int is_empty = (clen == 0);
                lb_push(lb, strdup(cur), is_empty ? 0 : lineno);
                if (!is_empty) lineno++;
                clen = 0;
            } else {
                if (clen + 2 > ccap) {
                    ccap = ccap ? ccap * 2 : 64;
                    cur = realloc(cur, (size_t)ccap);
                }
                cur[clen++] = c;
            }
        }
    }
    if (clen > 0) {
        cur[clen] = '\0';
        lb_push(lb, strdup(cur), lineno);
    }
    free(cur);
}

static int read_seekable_reverse(int fd, linebuf *lb)
{
    off_t fsize = lseek(fd, 0, SEEK_END);
    if (fsize < 0) return -1;
    if (fsize == 0) return 0;

    char chunk[CHUNK];
    off_t pos = fsize;

    char *tailbuf = NULL;
    int tlen = 0, tcap = 0;

    int trailing_nl = 0;
    {
        if (lseek(fd, fsize - 1, SEEK_SET) < 0) { free(tailbuf); return -1; }
        char last;
        if (read(fd, &last, 1) == 1 && last == '\n') trailing_nl = 1;
    }

    pos = fsize;
    while (pos > 0) {
        off_t to_read = pos < CHUNK ? pos : CHUNK;
        pos -= to_read;
        if (lseek(fd, pos, SEEK_SET) < 0) { free(tailbuf); return -1; }
        ssize_t got = read(fd, chunk, (size_t)to_read);
        if (got <= 0) break;

        for (ssize_t i = got - 1; i >= 0; i--) {
            char c = chunk[i];
            off_t abs_pos = pos + i;
            if (c == '\n') {
                if (abs_pos == fsize - 1 && trailing_nl) continue;

                char *rev = malloc((size_t)(tlen + 1));
                for (int k = 0; k < tlen; k++) rev[k] = tailbuf[tlen - 1 - k];
                rev[tlen] = '\0';
                lb_push(lb, rev, 0);
                tlen = 0;
            } else {
                if (tlen + 2 > tcap) {
                    tcap = tcap ? tcap * 2 : 64;
                    tailbuf = realloc(tailbuf, (size_t)tcap);
                }
                tailbuf[tlen++] = c;
            }
        }
    }

    if (tlen > 0) {
        char *rev = malloc((size_t)(tlen + 1));
        for (int k = 0; k < tlen; k++) rev[k] = tailbuf[tlen - 1 - k];
        rev[tlen] = '\0';
        lb_push(lb, rev, 0);
    }

    free(tailbuf);
    return 0;
}

static int process_fd(int fd, int flag_r, int flag_n, int is_seekable)
{
    linebuf lb = {0};

    if (flag_r && is_seekable && !flag_n) {
        if (read_seekable_reverse(fd, &lb) < 0) { lb_free(&lb); return -1; }
    } else {
        read_forward(fd, &lb);
        if (flag_r) reverse_lb(&lb);
    }

    print_lines(&lb, flag_n);
    lb_free(&lb);
    return 0;
}

int do_peek(char **argv, int argc)
{
    int flag_n = 0, flag_r = 0;
    int n_files = 0;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] != '\0') {
            int j = 1;
            while (arg[j]) {
                if (arg[j] == 'n') flag_n = 1;
                else if (arg[j] == 'r') flag_r = 1;
                else { fprintf(stderr, "peek: invalid syntax\n"); return -1; }
                j++;
            }
        } else {
            n_files++;
        }
    }

    if (n_files == 0) {
        process_fd(STDIN_FILENO, flag_r, flag_n, 0);
        return 0;
    }

    int ok = 1;
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] == '-' && arg[1] != '\0') continue;

        if (strcmp(arg, "-") == 0) {
            process_fd(STDIN_FILENO, flag_r, flag_n, 0);
            continue;
        }

        struct stat st;
        if (stat(arg, &st) != 0) {
            fprintf(stderr, "peek: no such file or directory\n");
            ok = 0;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            fprintf(stderr, "peek: is a directory\n");
            ok = 0;
            continue;
        }

        int fd = open(arg, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "peek: no such file or directory\n");
            ok = 0;
            continue;
        }

        int seekable = S_ISREG(st.st_mode);
        process_fd(fd, flag_r, flag_n, seekable);
        close(fd);
    }

    return ok ? 0 : -1;
}
