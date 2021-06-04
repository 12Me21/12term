#pragma once
#include <stdlib.h>

#include "buffer.h"

extern int ttynew(const char *line, char *cmd, const char *out, char **args);
extern size_t ttyread(Term* t);
extern void tty_hangup(void);
