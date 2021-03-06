#pragma once

#include "common.h"

typedef int Fd;

void tty_init(void);
size_t tty_read(void);
void tty_write(size_t n, const utf8 str[n]);
void tty_printf(const utf8* format, ...);
void tty_hangup(void);
void tty_resize(int w, int h, Px pw, Px ph);
bool tty_wait(Fd xfd, Nanosec timeout);
