// Loading fonts and looking up glyphs

#include <errno.h>
#include <math.h>
#include "xft/Xft.h"

#include "common.h"
#include "font.h"
#include "buffer.h"
#include "settings.h"
#include "x.h"

void font_init(void) {
	if (!FcInit())
		die("fontconfig init failed");
	xft_init();
	if (FT_Init_FreeType(&ft_library))
		die("freetype init failed");
}

static int ceildiv(int a, int b) {
	return (a+b-1)/b;
}

// This frees any existing fonts and loads new ones, based on `fontstr`.
// it also sets `W.cw` and `W.ch`.
void load_fonts(const utf8* fontstr, double fontsize) {
	print("loading pattern: %s\n", fontstr);
	
	fonts_free();
	
	FcPattern* pattern = FcNameParse((const FcChar8*)fontstr);
	
	if (!pattern)
		die("can't open font %s\n", fontstr);
	
	if (fontsize) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_SIZE, fontsize);
	}
	
	for (int i=0; i<4; i++) {
		if (!load_font(pattern, i, i&1, i&2))
			die("failed to load font");
		time_log("loaded font");
	}
	
	W.font_baseline = 10; //fonts[0].font->ascent;
	
	// messy. remember to call update_charsize
	W.cw = 9;//ceil(fonts[0].width);
	W.ch = 18;//ceil(fonts[0].height);
	
	FcPatternDestroy(pattern);
}

static int cell_fontstyle(const Cell* c) {
	return (c->attrs.weight==1) | (c->attrs.italic)<<1;
}

void cells_to_glyphs(int len, Cell cells[len], Glyph glyphs[len], bool cache) {	
	for (int i=0; i<len; i++) {
		Char chr = cells[i].chr;
		
		// skip blank cells
		if (cells[i].wide==-1 || chr==0 || chr==' ') {
			glyphs[i].chr = chr;
			glyphs[i].glyph = NULL;
			continue;
		}
		
		int style = cell_fontstyle(&cells[i]);
		
		if (cache && glyphs[i].chr==chr && glyphs[i].style==style) {
			// do nothing, cached data matches
		} else {
			glyphs[i].glyph = cache_lookup(chr, style);
		}
	}
}
// "_ascent"
//_ascent
 
void fonts_free(void) {
	/*for (int i=0; i<4; i++) {
		if (fonts[i].font) {
			FcPatternDestroy(fonts[i].pattern);
			XftFontClose(fonts[i].font);
			fonts[i].font = NULL;
		}
	}
	for (int i=0; i<frclen; i++)
	XftFontClose(frc[i].font);*/
}
