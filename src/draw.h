#pragma once

#include "buffer.h"
#include <X11/extensions/Xrender.h>

void draw(bool repaint_all);
void repaint(void);
void draw_free(void);
void draw_resize(int width, int height, bool charsize);
XRenderColor make_color(Color c);
void init_draw(void);
