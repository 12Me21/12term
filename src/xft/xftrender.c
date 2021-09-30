#include "xftint.h"

// todo: move the loadGlyphs call out of here and put it like, right after the glyph lookup in font.c  sleepy 💤
void XftGlyphRender1(Display* dpy, int	op, Picture src, XftFont* pub, Picture dst, int srcx, int srcy, int x, int y, FT_UInt g, int cw) {
	XftFontInt* font = (XftFontInt*)pub;
	if (!font->format)
		return;
	
	// Load missing glyphs
	FT_UInt missing[1];
	int nmissing = 0;
	FcBool glyphs_loaded = XftFontCheckGlyph(dpy, pub, FcTrue, g, missing, &nmissing);
	if (nmissing)
		XftFontLoadGlyphs(dpy, pub, FcTrue, missing, nmissing);
	
	if (!font->glyphset)
		goto bail1;
	Glyph wire = (Glyph)g;
	if (wire>=font->num_glyphs || !font->glyphs[wire])
		wire = 0;
	
	XftGlyph* glyph = font->glyphs[wire];
	if (glyph->picture) {
		XRenderComposite(dpy, PictOpOver, glyph->picture, None, dst, 0, 0, 0, 0, x - glyph->metrics.x + (cw - glyph->metrics.xOff)/2, y-glyph->metrics.y, glyph->metrics.width, glyph->metrics.height);
	} else {
		XRenderCompositeString32(dpy, op, src, dst, font->format, font->glyphset, srcx, srcy, x + (cw - glyph->metrics.xOff)/2, y, (unsigned int[]){wire}, 1);
		//printf("comp glyph %ld. %d %d %d\n", wire, glyph->metrics.x, glyph->metrics.width, glyph->metrics.xOff);
	}
 bail1:
	if (glyphs_loaded)
		_XftFontManageMemory(dpy, pub);
}
