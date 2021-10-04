#include "xftint.h"

// todo: move the loadGlyphs call out of here and put it like, right after the glyph lookup in font.c  sleepy 💤
// op - composite operation
// col - color (only used for normal monochrome glyphs)
// pub - font
// dst - destination picture
// x,y - destination position
// g - glyph id
// cw - width of character cell (this is messy. maybe would be better to pass the CENTER x coordinate
void XftGlyphRender1(int op, XRenderColor* col, XftFont* pub, Picture dst, int x, int y, FT_UInt g, int cw) {
	XftFontInt* font = (XftFontInt*)pub;
	if (!font->format)
		return;
	
	// Load missing glyphs
	FT_UInt missing[1];
	int nmissing = 0;
	FcBool glyphs_loaded = XftFontCheckGlyph(pub, true, g, missing, &nmissing);
	if (nmissing)
		XftFontLoadGlyphs(pub, true, missing, nmissing);
	
	if (!font->glyphset)
		goto bail1;
	Glyph wire = (Glyph)g;
	if (wire>=font->num_glyphs || !font->glyphs[wire])
		wire = 0;
	
	XftGlyph* glyph = font->glyphs[wire];
	Px center = (cw - glyph->metrics.xOff)/2;
	if (glyph->picture) {
		XRenderComposite(W.d, PictOpOver, glyph->picture, None, dst, 0, 0, 0, 0, x - glyph->metrics.x + center, y-glyph->metrics.y, glyph->metrics.width, glyph->metrics.height);
	} else {
		Picture src = XftDrawSrcPicture(col);
		XRenderCompositeString32(W.d, op, src, dst, font->format, font->glyphset, 0, 0, x + center, y, (unsigned int[]){wire}, 1);
	}
 bail1:
	if (glyphs_loaded)
		_XftFontManageMemory(pub);
}
