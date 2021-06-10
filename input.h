#pragma once

#include <X11/Xlib.h>

void on_keypress(XEvent *ev);
void on_focusin(XEvent* e);
void on_focusout(XEvent* e);

void xim_spot(int x, int y);

void init_input(void);
