#pragma once

#include <X11/Xft/Xft.h>

#include "buffer.h"

void draw(void);
void repaint(void);
void clear_background(void);
void draw_free(void);
void draw_resize(int width, int height);
XftColor make_color(Color c);
