#pragma once

#include <X11/Xft/Xft.h>

#include "buffer.h"

void draw(void);
void repaint(void);
void draw_free(void);
void draw_resize(int width, int height);
XftColor make_color(Color c);
void init_draw(void);
