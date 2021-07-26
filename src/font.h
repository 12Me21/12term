#pragma once

#include <X11/Xft/Xft.h>

#include "buffer.h" //nn

typedef struct {
	Char chr;
	int fontnum;
	FT_UInt glyph;
	XftFont* font;
} DrawnCell;

void init_fonts(const char* fontstr, double fontsize);
int make_glyphs(int len, XftGlyphFontSpec specs[len], Cell cells[len], int indexs[len], DrawnCell old[len]);
void fonts_free(void);