#pragma once
#include "common.h"

void draw_rotate_rows(int y1, int y2, int amount, bool screen_space);
void dirty_all(void);
void dirty_cursor(void);
