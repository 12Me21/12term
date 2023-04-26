#pragma once

#include <X11/Xlib.h>

typedef void (*HandlerFunc)(XEvent*);

extern const HandlerFunc HANDLERS[LASTEvent];

void xim_spot(int x, int y);
void init_input(void);
void clippaste(void);
//void simplecopy(void);
