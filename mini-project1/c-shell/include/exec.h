#ifndef exec_h
#define exec_h

#include <stddef.h>
#include "parser.h"

int do_exec(cmd *c);
int find_exec(char *name, char *out, size_t sz);

#endif
