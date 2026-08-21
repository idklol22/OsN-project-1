#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"

static int tl_push(tok_list *tl, tok_type type, char *val)
{
    if (tl->n == tl->cap) {
        tl->cap = tl->cap ? tl->cap * 2 : 16;
        tl->data = realloc(tl->data, (size_t)tl->cap * sizeof(tok));
        if (!tl->data) return -1;
    }
    tl->data[tl->n].type = type;
    tl->data[tl->n].val  = val ? strdup(val) : NULL;
    tl->n++;
    return 0;
}

static int wb_push(char **buf, int *blen, int *bcap, char c)
{
    if (*blen + 2 > *bcap) {
        *bcap = *bcap ? *bcap * 2 : 32;
        *buf  = realloc(*buf, (size_t)*bcap);
        if (!*buf) return -1;
    }
    (*buf)[(*blen)++] = c;
    (*buf)[*blen]     = '\0';
    return 0;
}

int lex(const char *line, tok_list *out)
{
    out->data = NULL;
    out->n    = 0;
    out->cap  = 0;

    const char *p  = line;
    char *wb       = NULL;
    int wlen = 0, wcap = 0, inword = 0;

    while (*p) {
        char ch = *p;

        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            if (inword) {
                tl_push(out, tok_word, wb);
                wlen = 0; if (wb) wb[0] = '\0';
                inword = 0;
            }
            p++;
            continue;
        }

        if (ch == '\\') {
            char nx = *(p + 1);
            if (nx == '\0' || nx == '\n') {
                fprintf(stderr, "cshell: invalid syntax\n");
                free(wb);
                tl_free(out);
                return -1;
            }
            inword = 1;
            wb_push(&wb, &wlen, &wcap, nx);
            p += 2;
            continue;
        }

        if (ch == '\'') {
            inword = 1;
            p++;
            while (*p && *p != '\'') {
                wb_push(&wb, &wlen, &wcap, *p);
                p++;
            }
            if (*p != '\'') {
                fprintf(stderr, "cshell: invalid syntax\n");
                free(wb);
                tl_free(out);
                return -1;
            }
            p++;
            continue;
        }

        if (ch == '"') {
            inword = 1;
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p + 1) != '\0') {
                    char nx = *(p + 1);
                    if (nx == '"' || nx == '\\') {
                        wb_push(&wb, &wlen, &wcap, nx);
                    } else {
                        wb_push(&wb, &wlen, &wcap, '\\');
                        wb_push(&wb, &wlen, &wcap, nx);
                    }
                    p += 2;
                } else {
                    wb_push(&wb, &wlen, &wcap, *p);
                    p++;
                }
            }
            if (*p != '"') {
                fprintf(stderr, "cshell: invalid syntax\n");
                free(wb);
                tl_free(out);
                return -1;
            }
            p++;
            continue;
        }

        if (ch == '|' || ch == '&' || ch == ';' || ch == '<' || ch == '>') {
            if (inword) {
                tl_push(out, tok_word, wb);
                wlen = 0; if (wb) wb[0] = '\0';
                inword = 0;
            }
            if (ch == '>') {
                if (*(p + 1) == '>') { tl_push(out, tok_gtgt, NULL); p += 2; }
                else                 { tl_push(out, tok_gt, NULL);   p++;    }
            } else if (ch == '|') { tl_push(out, tok_pipe, NULL); p++; }
            else if (ch == '&')   { tl_push(out, tok_amp,  NULL); p++; }
            else if (ch == ';')   { tl_push(out, tok_semi, NULL); p++; }
            else                  { tl_push(out, tok_lt,   NULL); p++; }
            continue;
        }

        inword = 1;
        wb_push(&wb, &wlen, &wcap, ch);
        p++;
    }

    if (inword)
        tl_push(out, tok_word, wb);

    free(wb);
    tl_push(out, tok_eof, NULL);
    return 0;
}

void tl_free(tok_list *tl)
{
    for (int i = 0; i < tl->n; i++)
        free(tl->data[i].val);
    free(tl->data);
    tl->data = NULL;
    tl->n = tl->cap = 0;
}
