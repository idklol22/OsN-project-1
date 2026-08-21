#ifndef lexer_h
#define lexer_h

#define shell_inmax 1024

typedef enum {
    tok_word,
    tok_pipe,
    tok_amp,
    tok_semi,
    tok_lt,
    tok_gt,
    tok_gtgt,
    tok_eof
} tok_type;

typedef struct {
    tok_type type;
    char *val;
} tok;

typedef struct {
    tok *data;
    int n;
    int cap;
} tok_list;

int lex(const char *line, tok_list *out);
void tl_free(tok_list *tl);

#endif
