#include "xftint.h"

XftDisplayInfo info;

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
	FOR (i, XFT_NUM_SOLID_COLOR) {
		info.colors[i].screen = -1;
		info.colors[i].pict = 0;
	}
	info.fonts = NULL;
	
	info.glyph_memory = 0;
	info.max_glyph_memory = (4 * 1024 * 1024);
	if (XftDebug() & XFT_DBG_CACHE)
		print("global max cache memory %zd\n", info.max_glyph_memory);
	
	//		print("XftDisplayInfoGet failed to initialize, Xft unhappy :(\n");
}

// Reduce memory usage in X server

static void validate_memory(void) {
	size_t glyph_memory = 0;
	for (XftFont* font=info.fonts; font; font=font->next) {
		glyph_memory += font->glyph_memory;
	}
	if (glyph_memory != info.glyph_memory)
		print("Display glyph cache incorrect has %zd bytes, should have %zd\n", info.glyph_memory, glyph_memory);
}

void xft_manage_memory(void) {
	if (!info.max_glyph_memory)
		return;
	
	if (XftDebug() & XFT_DBG_CACHE) {
		if (info.glyph_memory > info.max_glyph_memory)
			print("Reduce global memory from %zd to %zd\n",
			      info.glyph_memory, info.max_glyph_memory);
		validate_memory();
	}
	
	while (info.glyph_memory > info.max_glyph_memory) {
		size_t glyph_memory = rand() % info.glyph_memory;
		XftFont* font = info.fonts;
		while (font) {
			if (font->glyph_memory > glyph_memory) {
				xft_font_uncache_glyph(font);
				break;
			}
			font = font->next;
			glyph_memory -= font->glyph_memory;
		}
	}
	if (XftDebug() & XFT_DBG_CACHE)
		validate_memory();
}
