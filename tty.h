#pragma once
#include <stdlib.h>

extern int ttynew(const char *line, char *cmd, const char *out, char **args);
extern size_t ttyread(void);
