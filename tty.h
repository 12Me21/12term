#pragma once

#include "common.h"

typedef int Fd;

Fd tty_new(void);
size_t tty_read(void);
void tty_write(size_t n, const char str[n]);
void tty_printf(const char* format, ...);
void tty_hangup(void);
void tty_resize(int w, int h, Px pw, Px ph);
void tty_paste_text(int len, const char text[len]);
