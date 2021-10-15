#include "xftint.h"

// this function uses the intersection of the baseline and the center line as the origin
// ⎛note that the ascent and descent may vary depending on the font.       ⎞
// ⎜so even if a font is the correct height, it may not fit within the cell⎟
// ⎝ if it doesn't have the same metrics as the "main" font                ⎠

//     │           │    
// ╌╌╌╌╆━━━━━┿━━━━━╅╌╌╌╌
//     ┃     │     ┃ ⎫   
//     ┃     │     ┃ ⎪ 
//     ┃     │     ┃ ⎬ - ascent  
//     ┃     │     ┃ ⎪  
//     ┃     │     ┃ ⎭   
//  ───╂─────🟗─────╂── ← baseline
//     ┃     │     ┃ } - descent 
// ╌╌╌╌╄━━━━━┿━━━━━╃╌╌╌╌
//     │     ↑     │    
//       centerline
// (when drawing wide characters, the centerline will instead be on the boundary between the two cells.)

// todo: move the loadGlyphs call out of here and put it like, right after the glyph lookup in font.c  sleepy 💤
// op - composite operation
// col - color (only used for normal monochrome glyphs)
// pub - font
// dst - destination picture
// x,y - destination position (center, baseline)
// g - glyph id
void XftGlyphRender1(int op, XRenderColor col, XftFont* font, float x, int y, FT_UInt g) {
	// Load missing glyphs
	FT_UInt missing[1];
	int nmissing = 0;
	FcBool glyphs_loaded = XftFontCheckGlyph(font, true, g, missing, &nmissing);
	if (nmissing)
		XftFontLoadGlyphs(font, true, missing, nmissing);
	
	if (!font->glyphset)
		goto bail1;
	Glyph wire = (Glyph)g;
	if (wire>=font->num_glyphs || !font->glyphs[wire])
		wire = 0;
	
	XftGlyph* glyph = font->glyphs[wire];
	float half = glyph->metrics.xOff / 2.0f;
	int bx = (int)(x - half + 0.5);
	if (glyph->picture) {
		XRenderComposite(W.d, PictOpOver, glyph->picture, None, dst, 0, 0, 0, 0, bx + glyph->metrics.x, y - glyph->metrics.y, glyph->metrics.width, glyph->metrics.height);
	} else {
		Picture src = XftDrawSrcPicture(col);
		XRenderCompositeString32(W.d, op, src, dst, font->format, font->glyphset, 0, 0, bx, y, (unsigned int[]){wire}, 1);
	}
 bail1:
	if (glyphs_loaded)
		_XftFontManageMemory(font);
}
