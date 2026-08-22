#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include "hop.h"

static char home_dir[PATH_MAX];
char prevdir[4096] = {0};

typedef struct {
    char p[PATH_MAX];
    long cnt;
    long t;
} fitem;

static fitem db[1024];
static int db_sz = 0;
static char db_file[PATH_MAX];

static void read_db(void) {
    db_sz = 0;
    FILE *f = fopen(db_file, "r");
    if (!f) return;
    char buf[PATH_MAX + 100];
    while (fgets(buf, sizeof(buf), f) && db_sz < 1024) {
        int len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        char *p1 = strchr(buf, '\t');
        if (!p1) continue;
        *p1 = '\0';
        char *p2 = strchr(p1 + 1, '\t');
        if (!p2) continue;
        *p2 = '\0';
        strncpy(db[db_sz].p, buf, PATH_MAX - 1);
        db[db_sz].cnt = atol(p1 + 1);
        db[db_sz].t = atol(p2 + 1);
        db_sz++;
    }
    fclose(f);
}

static void write_db(void) {
    FILE *f = fopen(db_file, "w");
    if (!f) return;
    for (int i = 0; i < db_sz; i++) {
        fprintf(f, "%s\t%ld\t%ld\n", db[i].p, db[i].cnt, db[i].t);
    }
    fclose(f);
}

static void add_db(const char *path) {
    long now = (long)time(NULL);
    for (int i = 0; i < db_sz; i++) {
        if (strcmp(db[i].p, path) == 0) {
            db[i].cnt++;
            db[i].t = now;
            write_db();
            return;
        }
    }
    if (db_sz < 1024) {
        strncpy(db[db_sz].p, path, PATH_MAX - 1);
        db[db_sz].cnt = 1;
        db[db_sz].t = now;
        db_sz++;
        write_db();
    }
}

static double calc_score(fitem *item) {
    long diff = (long)time(NULL) - item->t;
    if (diff < 0) diff = 0;
    if (diff < 3600) return item->cnt * 4.0;
    if (diff < 86400) return item->cnt * 2.0;
    if (diff < 604800) return item->cnt * 0.5;
    return item->cnt * 0.25;
}

static int get_frec_match(const char *name) {
    int best_i = -1;
    double max_s = -1.0;
    for (int i = 0; i < db_sz; i++) {
        if (strstr(db[i].p, name) == NULL) continue;
        struct stat s;
        if (stat(db[i].p, &s) != 0 || !S_ISDIR(s.st_mode)) continue;
        double sc = calc_score(&db[i]);
        if (sc > max_s) {
            max_s = sc;
            best_i = i;
        }
    }
    return best_i;
}

void hop_init(const char *h) {
    strncpy(home_dir, h, PATH_MAX - 1);
    snprintf(db_file, sizeof(db_file), "%s/.cshell_frecency", h);
    read_db();
}

static int hop_single(const char *arg) {
    char cur[PATH_MAX];
    if (!getcwd(cur, sizeof(cur))) return -1;
    char target_dir[PATH_MAX];

    if (strcmp(arg, "~") == 0 || arg[0] == '\0') {
        strncpy(target_dir, home_dir, PATH_MAX - 1);
    } else if (strcmp(arg, ".") == 0) {
        return 0;
    } else if (strcmp(arg, "..") == 0) {
        strncpy(target_dir, "..", PATH_MAX - 1);
    } else if (strcmp(arg, "-") == 0) {
        if (prevdir[0] == '\0') return 0;
        strncpy(target_dir, prevdir, PATH_MAX - 1);
    } else {
        struct stat st;
        if (stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) {
            strncpy(target_dir, arg, PATH_MAX - 1);
        } else {
            int match = get_frec_match(arg);
            if (match < 0) {
                fprintf(stderr, "hop: no such directory\n");
                return -1;
            }
            strncpy(target_dir, db[match].p, PATH_MAX - 1);
        }
    }

    char old_dir[PATH_MAX];
    strncpy(old_dir, cur, PATH_MAX - 1);

    if (chdir(target_dir) != 0) {
        fprintf(stderr, "hop: no such directory\n");
        return -1;
    }

    strncpy(prevdir, old_dir, sizeof(prevdir) - 1);
    char next_dir[PATH_MAX];
    if (getcwd(next_dir, sizeof(next_dir))) {
        add_db(next_dir);
    }
    return 0;
}

int do_hop(char **a, int c) {
    if (c == 0) {
        return hop_single("~");
    }
    for (int i = 1; i < c; i++) {
        hop_single(a[i]);
    }
    return 0;
}
