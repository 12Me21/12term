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

void render_glyph(
	int op, // composite operation
	XRenderColor col, // color (only used for normal monochrome glyphs)
	Picture dst, // destination picture
	float x, // position (center)
	int y, // position (baseline)
	GlyphData* glyph // glyph
) {
	float half = glyph->metrics.xOff / 2.0f;
	int bx = (int)(x - half + 10000) - 10000; // add 10000 so the number isn't negative when rounded
	
	if (glyph->picture) {
		XRenderComposite(
			W.d, op, glyph->picture, None, dst,
			0, 0, 0, 0,
			bx + glyph->metrics.x, y - glyph->metrics.y,
			glyph->metrics.width, glyph->metrics.height
		);
	} else {
		Picture src = XftDrawSrcPicture(col);
		XRenderCompositeString32(
			W.d, op, src, dst, glyph->format, glyphset,
			0, 0, bx, y,
			(unsigned int[]){glyph->id}, 1
		);
	}
}
