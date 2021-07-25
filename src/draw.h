#pragma once

#include "buffer.h"

void draw(void);
void repaint(void);
void draw_free(void);
void draw_resize(int width, int height, bool charsize);
unsigned long alloc_color(Color c); // ugh
void init_draw(void);
void draw_copy_rows(int src, int dest, int num);
