#pragma once

#include "xft/Xft.h"

#include "common.h"
#include "buffer.h" //nn

#define Glyph _Glyph

typedef struct Glyph {
	GlyphData* glyph; //null if glyph is empty
	// keys for caching
	Char chr;
	char style; // whether bold/italic etc.
	// when turning cells into glyphs, if the prev 2 values match the new cell's, the cached glyph is used
	// todo: we need to store which cell the glyph is in, so we can handle combining chars
	int x;
} Glyph;

void load_fonts(const utf8* fontstr, double fontsize);
void fonts_free(void);
void cells_to_glyphs(int len, Cell cells[len], Glyph glyphs[len], bool cache);
void font_init(void);
