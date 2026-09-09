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

static int is_builtin(char *name)
{
    if (!name) return 0;
    if (strcmp(name, "hop") == 0) return 1;
    if (strcmp(name, "reveal") == 0) return 1;
    if (strcmp(name, "peek") == 0) return 1;
    if (strcmp(name, "locate") == 0) return 1;
    return 0;
}

static int run_pipeline(cmd **g, int n)
{
    if (n <= 0) return 0;

    for (int i = 0; i < n; i++) {
        for (redir *r = g[i]->redirs; r; r = r->nxt) {
            if (r->mode == redir_in) {
                int fd = open(r->fname, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "cshell: no such file or directory\n");
                    return -1;
                }
                close(fd);
            } else if (r->mode == redir_out || r->mode == redir_append) {
                int fl = O_WRONLY | O_CREAT | (r->mode == redir_out ? O_TRUNC : O_APPEND);
                int fd = open(r->fname, fl, 0644);
                if (fd < 0) {
                    fprintf(stderr, "cshell: unable to create file for writing\n");
                    return -1;
                }
                close(fd);
            }
        }
    }

    for (int i = 0; i < n; i++) {
        cmd *curr = g[i];
        if (!curr->argv || !curr->argv[0]) continue;
        if (!is_builtin(curr->argv[0])) {
            char runpath[2048];
            if (!find_exec(curr->argv[0], runpath, sizeof(runpath))) {
                char *err_name = curr->argv[0];
                if (err_name[0] == '%' && !strchr(err_name, '/')) err_name++;
                fprintf(stderr, "cshell: command not found (%s)\n", err_name);
                return -1;
            }
        }
    }

    if (n == 1 && strcmp(g[0]->argv[0], "hop") == 0) {
        do_hop(g[0]->argv, g[0]->argc);
        return 0;
    }

    int prev_fd = -1;
    pid_t *pids = malloc((size_t)n * sizeof(pid_t));

    for (int i = 0; i < n; i++) {
        int pipefd[2] = {-1, -1};
        if (i < n - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                free(pids);
                return -1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
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

            int outc = 0;
            for (redir *r = curr->redirs; r; r = r->nxt) {
                if (r->mode == redir_out || r->mode == redir_append) outc++;
            }

            if (outc > 1) {
                int pfd[2];
                if (pipe(pfd) == 0) {
                    pid_t tpid = fork();
                    if (tpid == 0) {
                        close(pfd[1]);
                        int fds[64];
                        int n_fds = 0;
                        for (redir *r = curr->redirs; r; r = r->nxt) {
                            if (r->mode == redir_out || r->mode == redir_append) {
                                int fl = O_WRONLY | O_CREAT | (r->mode == redir_out ? O_TRUNC : O_APPEND);
                                int fd = open(r->fname, fl, 0644);
                                if (fd >= 0 && n_fds < 64) fds[n_fds++] = fd;
                            }
                        }
                        char buf[1024];
                        ssize_t nr;
                        while ((nr = read(pfd[0], buf, sizeof(buf))) > 0) {
                            for (int k = 0; k < n_fds; k++) {
                                write(fds[k], buf, (size_t)nr);
                            }
                        }
                        for (int k = 0; k < n_fds; k++) close(fds[k]);
                        close(pfd[0]);
                        exit(0);
                    }
                    dup2(pfd[1], STDOUT_FILENO);
                    close(pfd[0]);
                    close(pfd[1]);
                }
            } else if (outc == 1) {
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
            if (strcmp(curr->argv[0], "hop") == 0) {
                int r = do_hop(curr->argv, curr->argc);
                exit(r == 0 ? 0 : 1);
            }

            char runpath[2048];
            find_exec(curr->argv[0], runpath, sizeof(runpath));

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

    free(pids);
    return 0;
}

int do_exec(cmd *c)
{
    if (!c) return 0;

    cmd *cur = c;
    while (cur) {
        cmd *pipe_start = cur;
        int n = 0;
        cmd *p = cur;
        while (p) {
            n++;
            if (p->sep != sep_pipe) break;
            p = p->nxt;
        }

        cmd *next_cmd = p ? p->nxt : NULL;

        cmd **g = malloc((size_t)n * sizeof(cmd *));
        cmd *t = pipe_start;
        for (int i = 0; i < n; i++) {
            g[i] = t;
            t = t->nxt;
        }

        int ret = run_pipeline(g, n);
        free(g);

        if (ret != 0) {
            return -1;
        }

        cur = next_cmd;
    }

    return 0;
}
