#pragma once

#include <X11/Xft/Xft.h>

#include "buffer.h" //nn

void init_fonts(const char* fontstr, double fontsize);
int xmakeglyphfontspecs(int len, XftGlyphFontSpec specs[len], const Cell cells[len], int x, int y);
void fonts_free(void);
