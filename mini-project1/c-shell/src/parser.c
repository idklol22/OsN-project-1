#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"

static tok_list *gl;
static int gi;

static tok *pk(void)  { return &gl->data[gi]; }
static tok *eat(void) { return &gl->data[gi++]; }

static cmd *mkc(void)
{
    cmd *c  = calloc(1, sizeof(cmd));
    c->argv = calloc(max_args, sizeof(char *));
    return c;
}

static void cmd_addarg(cmd *c, char *s)
{
    if (c->argc < max_args - 1)
        c->argv[c->argc++] = strdup(s);
}

static void cmd_addredir(cmd *c, int mode, char *fname)
{
    redir *r = calloc(1, sizeof(redir));
    r->mode  = mode;
    r->fname = strdup(fname);
    r->nxt   = NULL;
    if (!c->redirs) { c->redirs = r; return; }
    redir *tail = c->redirs;
    while (tail->nxt) tail = tail->nxt;
    tail->nxt = r;
}

static int do_arg(cmd *cur);

static int do_tgt(cmd *cur, int mode)
{
    if (pk()->type != tok_word) {
        printf("Invalid Syntax!\n");
        fflush(stdout);
        return -1;
    }
    tok *t = eat();
    cmd_addredir(cur, mode, t->val);
    return do_arg(cur);
}

static cmd *do_cmd(void);
static cmd *do_bg(void);

static int do_arg(cmd *cur)
{
    tok_type tt = pk()->type;
    if (tt == tok_eof) return 0;

    if (tt == tok_word) {
        cmd_addarg(cur, eat()->val);
        return do_arg(cur);
    }
    if (tt == tok_lt)   { eat(); return do_tgt(cur, redir_in);     }
    if (tt == tok_gt)   { eat(); return do_tgt(cur, redir_out);    }
    if (tt == tok_gtgt) { eat(); return do_tgt(cur, redir_append); }

    if (tt == tok_pipe) {
        eat();
        cur->sep = sep_pipe;
        cmd *nc = do_cmd();
        if (!nc) return -1;
        cur->nxt = nc;
        return 0;
    }
    if (tt == tok_semi) {
        eat();
        cur->sep = sep_semi;
        cmd *nc = do_cmd();
        if (!nc) return -1;
        cur->nxt = nc;
        return 0;
    }
    if (tt == tok_amp) {
        eat();
        cur->bg  = 1;
        cur->sep = sep_amp;
        cmd *nc = do_bg();
        if (nc == (cmd *)-1) return -1;
        cur->nxt = nc;
        return 0;
    }

    printf("Invalid Syntax!\n");
    fflush(stdout);
    return -1;
}

static cmd *do_cmd(void)
{
    if (pk()->type != tok_word) {
        printf("Invalid Syntax!\n");
        fflush(stdout);
        return NULL;
    }
    cmd *c = mkc();
    cmd_addarg(c, eat()->val);
    if (do_arg(c) < 0) { cmds_free(c); return NULL; }
    return c;
}

static cmd *do_bg(void)
{
    if (pk()->type == tok_eof) return NULL;
    if (pk()->type != tok_word) {
        printf("Invalid Syntax!\n");
        fflush(stdout);
        return (cmd *)-1;
    }
    cmd *c = mkc();
    cmd_addarg(c, eat()->val);
    if (do_arg(c) < 0) { cmds_free(c); return (cmd *)-1; }
    return c;
}

cmd *parse(tok_list *tl)
{
    gl = tl;
    gi = 0;

    if (pk()->type == tok_eof) return NULL;

    if (pk()->type != tok_word) {
        printf("Invalid Syntax!\n");
        fflush(stdout);
        return NULL;
    }

    cmd *head = mkc();
    cmd_addarg(head, eat()->val);
    if (do_arg(head) < 0) { cmds_free(head); return NULL; }
    return head;
}

void cmds_free(cmd *c)
{
    while (c) {
        cmd *nx = c->nxt;
        for (int i = 0; i < c->argc; i++) free(c->argv[i]);
        free(c->argv);
        redir *r = c->redirs;
        while (r) { redir *rn = r->nxt; free(r->fname); free(r); r = rn; }
        free(c);
        c = nx;
    }
}
