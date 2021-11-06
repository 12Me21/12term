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
	if (FT_Init_FreeType(&ft_library))
		die("freetype init failed");
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
