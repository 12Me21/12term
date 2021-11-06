#include "xftint.h"

XftFormat xft_formats[PictStandardNUM];

static void init_format(int type) {
	XRenderPictFormat* format = XRenderFindStandardFormat(W.d, type);
	// todo: check
	xft_formats[type] = (XftFormat){
		.format = format,
		.glyphset = XRenderCreateGlyphSet(W.d, format),
		.next_glyph = 0,
	};
}

void xft_init(void) {
	if (XftDebug() & XFT_DBG_RENDER) {
		print("XftDisplayInfoGet Default visual 0x%x ", (int)W.vis->visualid);
		if (W.format) {
			if (W.format->type == PictTypeDirect) {
				print("format %d,%d,%d,%d\n",
				      W.format->direct.alpha,
				      W.format->direct.red,
				      W.format->direct.green,
				      W.format->direct.blue);
			} else {
				print("format indexed\n");
			}
		} else
			print("No Render format for default visual\n");
		print("XftDisplayInfoGet initialized");
	}
	init_format(PictStandardARGB32);
	init_format(PictStandardA8);
	init_format(PictStandardA1);
	
	//		print("XftDisplayInfoGet failed to initialize, Xft unhappy :(\n");
}
