#pragma once
#include <stdlib.h>

#include "buffer.h"

extern int tty_new(char** args);
extern size_t ttyread(void);
extern void tty_write(size_t n, const char str[n]);
extern void tty_printf(const char* format, ...);
extern void tty_hangup(void);
extern void tty_resize(int w, int h);
