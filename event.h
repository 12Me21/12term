#pragma once

#include <X11/Xlib.h>

typedef void (*HandlerFunc)(XEvent*);

const HandlerFunc HANDLERS[LASTEvent];

void xim_spot(int x, int y);
void init_input(void);
