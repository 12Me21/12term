#pragma once
#include <stdlib.h>

#include "buffer.h"

extern int tty_new(char** args);
extern size_t ttyread(Term* t);
extern void tty_write(Term* t, size_t n, const char str[n]);
extern void tty_printf(Term* t, const char* format, ...);
extern void tty_hangup(void);
