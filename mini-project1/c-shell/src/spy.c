#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <limits.h>
#include "spy.h"

static int is_all_digits(const char *str)
{
    if (!str || !*str) return 0;
    for (int i = 0; str[i]; i++) {
        if (!isdigit((unsigned char)str[i])) return 0;
    }
    return 1;
}

static const char *get_type_from_mode(mode_t mode)
{
    if (S_ISREG(mode)) return "REG";
    if (S_ISDIR(mode)) return "DIR";
    if (S_ISCHR(mode)) return "CHR";
    if (S_ISBLK(mode)) return "BLK";
    if (S_ISFIFO(mode)) return "FIFO";
    if (S_ISSOCK(mode)) return "SOCK";
    if (S_ISLNK(mode)) return "LNK";
    return "REG";
}

static int compare_ints(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return ia - ib;
}

int do_spy(char **argv, int argc)
{
    if (argc > 2) {
        fprintf(stderr, "spy: invalid syntax\n");
        return -1;
    }

    pid_t target_pid;
    if (argc == 1) {
        target_pid = getpid();
    } else {
        if (!is_all_digits(argv[1])) {
            fprintf(stderr, "spy: invalid syntax\n");
            return -1;
        }
        long val = strtol(argv[1], NULL, 10);
        if (val <= 0) {
            fprintf(stderr, "spy: no such process\n");
            return -1;
        }
        target_pid = (pid_t)val;
    }

    char procdir[64];
    snprintf(procdir, sizeof(procdir), "/proc/%d", (int)target_pid);
    struct stat st;
    if (stat(procdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "spy: no such process\n");
        return -1;
    }

    printf("%-6s %-6s %-6s %s\n", "PID", "FD", "TYPE", "PATH");

    // 1. Current working directory (cwd)
    char cwd_link[128], cwd_path[PATH_MAX];
    snprintf(cwd_link, sizeof(cwd_link), "/proc/%d/cwd", (int)target_pid);
    ssize_t len = readlink(cwd_link, cwd_path, sizeof(cwd_path) - 1);
    if (len > 0) {
        cwd_path[len] = '\0';
        const char *tp = "DIR";
        struct stat s;
        if (stat(cwd_link, &s) == 0) tp = get_type_from_mode(s.st_mode);
        printf("%-6d %-6s %-6s %s\n", (int)target_pid, "cwd", tp, cwd_path);
    }

    // 2. Executable file (txt)
    char exe_link[128], txt_path[PATH_MAX] = {0};
    snprintf(exe_link, sizeof(exe_link), "/proc/%d/exe", (int)target_pid);
    len = readlink(exe_link, txt_path, sizeof(txt_path) - 1);
    if (len > 0) {
        txt_path[len] = '\0';
        const char *tp = "REG";
        struct stat s;
        if (stat(exe_link, &s) == 0) tp = get_type_from_mode(s.st_mode);
        printf("%-6d %-6s %-6s %s\n", (int)target_pid, "txt", tp, txt_path);
    }

    // 3. Memory-mapped files (mem)
    char maps_file[128];
    snprintf(maps_file, sizeof(maps_file), "/proc/%d/maps", (int)target_pid);
    FILE *mf = fopen(maps_file, "r");
    if (mf) {
        char line[2048];
        char seen_paths[512][PATH_MAX];
        int num_seen = 0;

        while (fgets(line, sizeof(line), mf)) {
            // Address perms offset dev inode pathname
            char *p = line;
            // Skip 5 whitespace-delimited fields
            for (int f = 0; f < 5; f++) {
                while (*p && !isspace((unsigned char)*p)) p++;
                while (*p && isspace((unsigned char)*p)) p++;
            }
            if (!*p || *p != '/') continue;

            // Trim trailing newline or whitespace
            char *end = p + strlen(p) - 1;
            while (end > p && isspace((unsigned char)*end)) {
                *end = '\0';
                end--;
            }

            // Exclude the executable text file (already shown as txt)
            if (txt_path[0] && strcmp(p, txt_path) == 0) continue;

            // Ensure unique
            int already_seen = 0;
            for (int i = 0; i < num_seen; i++) {
                if (strcmp(seen_paths[i], p) == 0) {
                    already_seen = 1;
                    break;
                }
            }
            if (already_seen) continue;

            if (num_seen < 512) {
                strncpy(seen_paths[num_seen], p, PATH_MAX - 1);
                seen_paths[num_seen][PATH_MAX - 1] = '\0';
                num_seen++;
            }

            const char *tp = "REG";
            struct stat s;
            if (stat(p, &s) == 0) tp = get_type_from_mode(s.st_mode);

            printf("%-6d %-6s %-6s %s\n", (int)target_pid, "mem", tp, p);
        }
        fclose(mf);
    }

    // 4. Numeric file descriptors
    char fd_dir[128];
    snprintf(fd_dir, sizeof(fd_dir), "/proc/%d/fd", (int)target_pid);
    DIR *dir = opendir(fd_dir);
    if (dir) {
        int fd_list[1024];
        int fd_count = 0;
        struct dirent *ent;

        while ((ent = readdir(dir)) != NULL) {
            if (is_all_digits(ent->d_name)) {
                if (fd_count < 1024) {
                    fd_list[fd_count++] = atoi(ent->d_name);
                }
            }
        }
        closedir(dir);

        qsort(fd_list, fd_count, sizeof(int), compare_ints);

        for (int i = 0; i < fd_count; i++) {
            char linkpath[128], targetpath[PATH_MAX];
            snprintf(linkpath, sizeof(linkpath), "/proc/%d/fd/%d", (int)target_pid, fd_list[i]);
            len = readlink(linkpath, targetpath, sizeof(targetpath) - 1);
            if (len <= 0) continue;
            targetpath[len] = '\0';

            const char *tp = "REG";
            struct stat s;
            if (stat(linkpath, &s) == 0) {
                tp = get_type_from_mode(s.st_mode);
            } else if (lstat(linkpath, &s) == 0) {
                tp = get_type_from_mode(s.st_mode);
            } else if (strncmp(targetpath, "pipe:", 5) == 0) {
                tp = "FIFO";
            } else if (strncmp(targetpath, "socket:", 7) == 0) {
                tp = "SOCK";
            }

            char fd_str[16];
            snprintf(fd_str, sizeof(fd_str), "%d", fd_list[i]);
            printf("%-6d %-6s %-6s %s\n", (int)target_pid, fd_str, tp, targetpath);
        }
    }

    fflush(stdout);
    return 0;
}
