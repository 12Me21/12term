#pragma once

#include <X11/extensions/Xrender.h>

#include "../common.h"

// this contains all the info needed to render a glyph
typedef struct GlyphData {
	XGlyphInfo metrics;
	union {
		Glyph id; // index in glyphset
		Picture picture; // for color glyphs
	};
	char type; // 0 = doesn't exist, 1 = glyph, 2 = picture
	char format; // PictStandard___
} GlyphData;

GlyphData* cache_lookup(Char chr, uint8_t style);

void load_fonts(const utf8* fontstr, double fontsize);
void fonts_free(void);

void render_glyph(XRenderColor col, Picture dst, float x, int y, GlyphData* glyph);

void font_init(void);

void close_all(void);
