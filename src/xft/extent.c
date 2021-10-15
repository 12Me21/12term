#include "xftint.h"

// so this file succinctly illustrates a problem:
// first, it calls XftCharIndex, which looks up the glyph index
// then calls xftfontcheckglyph, which checks if the glyph it just looked up is loaded, and then calls loadglyph

// this sequence of lookup->check->load is repeated many times, and would probably be more efficient if it was combined into one step, hmm?

//void XftWhatever(Display* d, XftFont* pub, int len, Char chars[len], Glyph glyphs[len]); etc. (well also handling fallbacks phaps egg.g.gfhsdfkd)
// and then another function to draw the glyphs which doesnt check if they are loaded

// note: we also want to do multiple glyphs at once. yes that means ME
// so, what i need to do is
// 1: figure out which chars onscreen have changed
// 2: put those in a big list, then call the whole lookup->check->load thing
//  this gives me a bunch of glyphs (i.e. font+glyph id) which are known to be loaded
// these can then be rendered without checking

void XftGlyphExtents(XftFont* font, const FT_UInt* glyphs, int nglyphs, XGlyphInfo* extents) {
	FT_UInt missing[XFT_NMISSING];
	
	const FT_UInt* g = glyphs;
	int n = nglyphs;
	int nmissing = 0;
	bool glyphs_loaded = false;
	while (n--)
		if (XftFontCheckGlyph(font, false, *g++, missing, &nmissing))
			glyphs_loaded = true;
	if (nmissing)
		XftFontLoadGlyphs(font, false, missing, nmissing);
	g = glyphs;
	n = nglyphs;
	XftGlyph* xftg = NULL;
	FT_UInt glyph;
	while (n) {
		glyph = *g++;
		n--;
		if (glyph < font->num_glyphs && (xftg = font->glyphs[glyph]))
			break;
	}
	if (n == 0) {
		if (xftg)
			*extents = xftg->metrics;
		else
			memset (extents, '\0', sizeof(*extents));
	} else {
		int x = 0;
		int y = 0;
		int overall_left = x - xftg->metrics.x;
		int overall_top = y - xftg->metrics.y;
		int overall_right = overall_left + (int) xftg->metrics.width;
		int overall_bottom = overall_top + (int) xftg->metrics.height;
		x += xftg->metrics.xOff;
		y += xftg->metrics.yOff;
		while (n--) {
			glyph = *g++;
			if (glyph < font->num_glyphs && (xftg = font->glyphs[glyph])) {
				int left = x - xftg->metrics.x;
				int top = y - xftg->metrics.y;
				int right = left + (int) xftg->metrics.width;
				int bottom = top + (int) xftg->metrics.height;
				if (left < overall_left)
					overall_left = left;
				if (top < overall_top)
					overall_top = top;
				if (right > overall_right)
					overall_right = right;
				if (bottom > overall_bottom)
					overall_bottom = bottom;
				x += xftg->metrics.xOff;
				y += xftg->metrics.yOff;
			}
		}
		extents->x = -overall_left;
		extents->y = -overall_top;
		extents->width = overall_right - overall_left;
		extents->height = overall_bottom - overall_top;
		extents->xOff = x;
		extents->yOff = y;
	}
	if (glyphs_loaded)
		_XftFontManageMemory(font);
}

void XftTextExtents32(XftFont* font, const FcChar32* string, int len, XGlyphInfo* extents) {
	FT_UInt glyphs[len];
	for (int i=0; i<len; i++)
		glyphs[i] = XftCharIndex(font, string[i]);
	XftGlyphExtents(font, glyphs, len, extents);
}
