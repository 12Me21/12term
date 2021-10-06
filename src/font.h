#pragma once

#include "xft/Xft.h"

#include "common.h"
#include "buffer.h" //nn

typedef struct {
	Char chr;
	int fontnum;
	FT_UInt glyph;
	XftFont* font;
} DrawnCell;

#define Glyph _Glyph

typedef struct Glyph {
	FT_UInt glyph;
	XftFont* font; //null if glyph is empty
	Px x;
	Px y;
	//bool wide;
	// keys for caching
	Char chr;
	char style; // whether bold/italic etc.
	// when turning cells into glyphs, if the prev 2 values match the new cell's, the cached glyph is used
} Glyph;

void load_fonts(const utf8* fontstr, double fontsize);
int make_glyphs(int len, XftGlyphFontSpec specs[len], Cell cells[len], int indexs[len], DrawnCell old[len]);
void fonts_free(void);
void cells_to_glyphs(int len, Cell cells[len], Glyph glyphs[len], bool cache);
void font_init(void);
