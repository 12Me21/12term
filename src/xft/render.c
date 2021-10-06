#include "xftint.h"
#include <xcb/render.h>
#include <xcb/xcb_renderutil.h>

static xcb_render_picture_t pict;

// improve caching here
xcb_render_picture_t XftDrawSrcPicture(const xcb_render_color_t color) {
	/*if (pict)
		XRenderFreePicture(W.d, pict);
		return pict =XRenderCreateSolidFill(W.d, color);*/
	
	// See if there's one already available
	for (int i=0; i<XFT_NUM_SOLID_COLOR; i++) {
		if (info.colors[i].pict && info.colors[i].screen == W.scr && !memcmp(&color, &info.colors[i].color, sizeof(xcb_render_color_t)))
			return info.colors[i].pict;
	}
	// Pick one to replace at random
	int i = (unsigned int)rand() % XFT_NUM_SOLID_COLOR;
	
	// Free any existing entry
	if (info.colors[i].pict)
		xcb_render_free_picture_checked(W.c, info.colors[i].pict);
	// Create picture
	// is it worth caching this anymore?
	xcb_render_picture_t p = xcb_generate_id(W.c);
	info.colors[i].pict = p;
	xcb_render_create_solid_fill_checked(W.c, p, color);
	
	info.colors[i].color = color;
	info.colors[i].screen = W.scr;

	return info.colors[i].pict;
}

// todo: move the loadGlyphs call out of here and put it like, right after the glyph lookup in font.c  sleepy 💤
// op - composite operation
// col - color (only used for normal monochrome glyphs)
// pub - font
// dst - destination picture
// x,y - destination position
// g - glyph id
// cw - width of character cell (this is messy. maybe would be better to pass the CENTER x coordinate
void XftGlyphRender1(int op, xcb_render_color_t col, XftFont* pub, xcb_render_picture_t dst, int x, int y, FT_UInt g, int cw) {
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
	xcb_render_glyph_t wire = g;
	if (wire>=font->num_glyphs || !font->glyphs[wire])
		wire = 0;
	
	XftGlyph* glyph = font->glyphs[wire];
	Px center = (cw - glyph->metrics.x_off)/2;
	if (glyph->picture) {
		xcb_render_composite(W.c, op, glyph->picture, XCB_RENDER_PICTURE_NONE, dst, 0, 0, 0, 0, x - glyph->metrics.x + center, y-glyph->metrics.y, glyph->metrics.width, glyph->metrics.height);
	} else {
		xcb_render_picture_t src = XftDrawSrcPicture(col);
		// todo: figure out the real best way to do this
		xcb_render_util_composite_text_stream_t* ts = xcb_render_util_composite_text_stream(font->glyphset, 1, 0);
		// add the glyphs
		xcb_render_util_glyphs_32(ts, x, y, 1, &wire);
		// composite
		xcb_render_util_composite_text(
			W.c,
			op,
			src,
			dst,
			font->format->id,
			0, // src x
			0, // src y
			ts); // txt stream
		xcb_render_util_composite_text_free(ts);
		//XRenderCompositeString32(W.d, op, src, dst, (XRenderPictFormat*)font->format, font->glyphset, 0, 0, x + center, y, (unsigned int[]){wire}, 1);
	}
 bail1:
	if (glyphs_loaded)
		_XftFontManageMemory(pub);
}
