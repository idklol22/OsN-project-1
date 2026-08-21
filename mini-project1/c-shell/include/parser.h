#ifndef parser_h
#define parser_h

#include "lexer.h"

#define sep_none 0
#define sep_pipe 1
#define sep_semi 2
#define sep_amp  3

#define redir_in     0
#define redir_out    1
#define redir_append 2

#define max_args 256

typedef struct redir {
    int mode;
    char *fname;
    struct redir *nxt;
} redir;

typedef struct cmd {
    char **argv;
    int argc;
    redir *redirs;
    int bg;
    int sep;
    struct cmd *nxt;
} cmd;

cmd *parse(tok_list *tl);
void cmds_free(cmd *c);

#endif
