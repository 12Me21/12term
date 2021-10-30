#include "xftint.h"
#include "../buffer.h"

// improve caching here
Picture XftDrawSrcPicture(const XRenderColor color) {
	/*if (pict)
		XRenderFreePicture(W.d, pict);
		return pict =XRenderCreateSolidFill(W.d, color);*/
	
	// See if there's one already available
	FOR (i, XFT_NUM_SOLID_COLOR) {
		if (info.colors[i].pict && info.colors[i].screen == W.scr && !memcmp(&color, &info.colors[i].color, sizeof(XRenderColor)))
			return info.colors[i].pict;
	}
	// Pick one to replace at random
	int i = (unsigned int)rand() % XFT_NUM_SOLID_COLOR;
	
	// Free any existing entry
	if (info.colors[i].pict)
		XRenderFreePicture(W.d, info.colors[i].pict);
	// Create picture
	// is it worth caching this anymore?
	info.colors[i].pict = XRenderCreateSolidFill(W.d, &color);
	
	info.colors[i].color = color;
	info.colors[i].screen = W.scr;

	return info.colors[i].pict;
}

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
void XftGlyphRender1(int op, XRenderColor col, XftFont* font, Picture dst, float x, int y, FT_UInt wire) {
	// Load missing glyphs
	FT_UInt missing[1];
	int nmissing = 0;
	FcBool glyphs_loaded = XftFontCheckGlyph(font, wire, missing, &nmissing);
	if (nmissing)
		XftFontLoadGlyphs(font, missing, nmissing);
	
	if (!font->glyphset)
		goto bail1;
	if (wire>=font->num_glyphs || !font->glyphs[wire])
		wire = 0;
	
	XftGlyph* glyph = font->glyphs[wire];
	float half = glyph->metrics.xOff / 2.0f;
	int bx = (int)(x - half + 10000) - 10000; // add 10000 so the number isn't negative when rounded
	
	if (glyph->picture) {
		XRenderComposite(W.d, op, glyph->picture, None, dst, 0, 0, 0, 0,
			bx + glyph->metrics.x, y - glyph->metrics.y, glyph->metrics.width, glyph->metrics.height);
	} else {
		Picture src = XftDrawSrcPicture(col);
		XRenderCompositeString32(W.d, op, src, dst, font->format, font->glyphset, 0, 0, bx, y, (unsigned int[]){wire}, 1);
	}
 bail1:
	if (glyphs_loaded)
		xft_font_manage_memory(font);
}
