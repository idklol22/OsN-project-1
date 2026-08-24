#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "exec.h"
#include "hop.h"
#include "reveal.h"
#include "peek.h"
#include "locate.h"

static int find_exec(char *name, char *out, size_t sz)
{
    if (strchr(name, '/') != NULL) {
        strncpy(out, name, sz - 1);
        out[sz - 1] = '\0';
        struct stat st;
        if (stat(out, &st) == 0 && S_ISREG(st.st_mode) && access(out, X_OK) == 0)
            return 1;
        return 0;
    }

    char *tname = name;
    int check_cwd = 1;
    if (name[0] == '%') {
        tname = name + 1;
        check_cwd = 0;
    }

    if (check_cwd) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd))) {
            snprintf(out, sz, "%s/%s", cwd, tname);
            struct stat st;
            if (stat(out, &st) == 0 && S_ISREG(st.st_mode) && access(out, X_OK) == 0)
                return 1;
        }
    }

    char *pth = getenv("PATH");
    if (pth) {
        char *pc = strdup(pth);
        char *tok = strtok(pc, ":");
        while (tok) {
            snprintf(out, sz, "%s/%s", tok, tname);
            struct stat st;
            if (stat(out, &st) == 0 && S_ISREG(st.st_mode) && access(out, X_OK) == 0) {
                free(pc);
                return 1;
            }
            tok = strtok(NULL, ":");
        }
        free(pc);
    }

    return 0;
}

int do_exec(cmd *c)
{
    if (!c || c->argc == 0) return 0;

    if (strcmp(c->argv[0], "hop") == 0) {
        do_hop(c->argv, c->argc);
        return 0;
    }

    cmd *cur = c;
    int n = 0;
    while (cur) {
        n++;
        if (cur->sep != sep_pipe) break;
        cur = cur->nxt;
    }

    cmd **g = malloc((size_t)n * sizeof(cmd *));
    cur = c;
    for (int i = 0; i < n; i++) {
        g[i] = cur;
        cur = cur->nxt;
    }

    for (int i = 0; i < n; i++) {
        for (redir *r = g[i]->redirs; r; r = r->nxt) {
            if (r->mode == redir_in) {
                int fd = open(r->fname, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "cshell: no such file or directory\n");
                    free(g);
                    return -1;
                }
                close(fd);
            } else if (r->mode == redir_out || r->mode == redir_append) {
                int fl = O_WRONLY | O_CREAT | (r->mode == redir_out ? O_TRUNC : O_APPEND);
                int fd = open(r->fname, fl, 0644);
                if (fd < 0) {
                    fprintf(stderr, "cshell: unable to create file for writing\n");
                    free(g);
                    return -1;
                }
                close(fd);
            }
        }
    }

    int prev_fd = -1;
    pid_t *pids = malloc((size_t)n * sizeof(pid_t));

    for (int i = 0; i < n; i++) {
        int pipefd[2] = {-1, -1};
        if (i < n - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                free(g);
                free(pids);
                return -1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            free(g);
            free(pids);
            return -1;
        }

        if (pid == 0) {
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            if (i < n - 1) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }

            cmd *curr = g[i];
            int inc = 0;
            for (redir *r = curr->redirs; r; r = r->nxt) {
                if (r->mode == redir_in) inc++;
            }

            if (inc == 1) {
                for (redir *r = curr->redirs; r; r = r->nxt) {
                    if (r->mode == redir_in) {
                        int fd = open(r->fname, O_RDONLY);
                        if (fd >= 0) {
                            dup2(fd, STDIN_FILENO);
                            close(fd);
                        }
                        break;
                    }
                }
            } else if (inc > 1) {
                int pfd[2];
                if (pipe(pfd) == 0) {
                    pid_t fpid = fork();
                    if (fpid == 0) {
                        close(pfd[0]);
                        for (redir *r = curr->redirs; r; r = r->nxt) {
                            if (r->mode == redir_in) {
                                int fd = open(r->fname, O_RDONLY);
                                if (fd >= 0) {
                                    char buf[1024];
                                    ssize_t nr;
                                    while ((nr = read(fd, buf, sizeof(buf))) > 0) {
                                        write(pfd[1], buf, (size_t)nr);
                                    }
                                    close(fd);
                                }
                            }
                        }
                        close(pfd[1]);
                        exit(0);
                    }
                    dup2(pfd[0], STDIN_FILENO);
                    close(pfd[0]);
                    close(pfd[1]);
                }
            }

            for (redir *r = curr->redirs; r; r = r->nxt) {
                if (r->mode == redir_out || r->mode == redir_append) {
                    int fl = O_WRONLY | O_CREAT | (r->mode == redir_out ? O_TRUNC : O_APPEND);
                    int fd = open(r->fname, fl, 0644);
                    if (fd >= 0) {
                        dup2(fd, STDOUT_FILENO);
                        close(fd);
                    }
                }
            }

            if (strcmp(curr->argv[0], "reveal") == 0) {
                int r = do_reveal(curr->argv, curr->argc);
                exit(r == 0 ? 0 : 1);
            }
            if (strcmp(curr->argv[0], "peek") == 0) {
                int r = do_peek(curr->argv, curr->argc);
                exit(r == 0 ? 0 : 1);
            }
            if (strcmp(curr->argv[0], "locate") == 0) {
                int r = do_locate(curr->argv, curr->argc);
                exit(r == 0 ? 0 : 1);
            }

            char runpath[2048];
            if (!find_exec(curr->argv[0], runpath, sizeof(runpath))) {
                char *err_name = curr->argv[0];
                if (err_name[0] == '%' && !strchr(err_name, '/')) err_name++;
                fprintf(stderr, "cshell: command not found (%s)\n", err_name);
                exit(1);
            }

            if (curr->argv[0][0] == '%' && !strchr(curr->argv[0], '/')) {
                curr->argv[0] = curr->argv[0] + 1;
            }

            execv(runpath, curr->argv);
            perror("execv");
            exit(1);
        }

        pids[i] = pid;
        if (prev_fd != -1) close(prev_fd);
        if (i < n - 1) {
            close(pipefd[1]);
            prev_fd = pipefd[0];
        }
    }

    for (int i = 0; i < n; i++) {
        int st;
        waitpid(pids[i], &st, 0);
    }

    free(g);
    free(pids);
    return 0;
}
