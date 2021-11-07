#include "xftint.h"
#include "../buffer.h"

// color cache
static struct {
	XRenderColor color;
	Picture pict;
} colors[XFT_NUM_SOLID_COLOR] = {0};

// todo: improve caching here
Picture XftDrawSrcPicture(const XRenderColor color) {
	// See if there's one already available
	FOR (i, XFT_NUM_SOLID_COLOR) {
		if (colors[i].pict && !memcmp(&color, &colors[i].color, sizeof(XRenderColor)))
			return colors[i].pict;
	}
	// Pick one to replace at random
	int i = (unsigned int)rand() % XFT_NUM_SOLID_COLOR;
	
	// Free any existing entry
	if (colors[i].pict)
		XRenderFreePicture(W.d, colors[i].pict);
	// Create picture
	// is it worth caching this anymore?
	colors[i].pict = XRenderCreateSolidFill(W.d, &color);
	colors[i].color = color;

	return colors[i].pict;
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
	XRenderColor col, // color (only used for normal monochrome glyphs)
	Picture dst, // destination picture
	float x, // position (center)
	int y, // position (baseline)
	GlyphData* glyph // glyph
) {
	float half = glyph->metrics.xOff / 2.0f;
	int bx = (int)(x - half + 10000) - 10000; // add 10000 so the number isn't negative when rounded
	
	// regular glyph
	if (glyph->type==1) {
		Picture src = XftDrawSrcPicture(col);
		XftFormat* format = &xft_formats[glyph->format];
		XRenderCompositeString32(
			W.d, PictOpOver,
			src, dst, // color, dest
			format->format, // format (is this correct?)
			format->glyphset,
			0, 0, // source pos
			bx, y, // dest pos
			(unsigned int[]){glyph->id}, 1 // list of glyphs
		);
	// color image glyph (i.e. emoji)
	} else if (glyph->type==2) {
		XRenderComposite(
			W.d, PictOpOver,
			glyph->picture, None, dst, // source, mask, dest
			0, 0, 0, 0, // source/mask pos
			bx-glyph->metrics.x, y-glyph->metrics.y, // dest pos
			glyph->metrics.width, glyph->metrics.height // size
		);
	// invalid
	} else {
		print("tried to render unloaded glyph?\n");
	}
}
