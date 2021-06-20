#pragma once

#include "buffer.h"

void draw(void);
void repaint(void);
void draw_free(void);
void draw_resize(int width, int height);
unsigned long alloc_color(Color c); // ugh
void init_draw(void);
