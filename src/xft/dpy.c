#include "xftint.h"

XftDisplayInfo info;

void XftDisplayInfoInit(void) {
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
	for (int i=0; i<XFT_NUM_SOLID_COLOR; i++) {
		info.colors[i].screen = -1;
		info.colors[i].pict = 0;
	}
	info.fonts = NULL;
	
	info.glyph_memory = 0;
	info.max_glyph_memory = XFT_DPY_MAX_GLYPH_MEMORY;//XftDefaultGetInteger(XFT_MAX_GLYPH_MEMORY, XFT_DPY_MAX_GLYPH_MEMORY);
	if (XftDebug() & XFT_DBG_CACHE)
		print("global max cache memory %ld\n", info.max_glyph_memory);
	
	info.num_unref_fonts = 0;
	info.max_unref_fonts = XFT_DPY_MAX_UNREF_FONTS;//XftDefaultGetInteger(XFT_MAX_UNREF_FONTS, XFT_DPY_MAX_UNREF_FONTS);
	if (XftDebug() & XFT_DBG_CACHE)
		print("global max unref fonts %d\n", info.max_unref_fonts);
	
	// bail1:
	if (XftDebug() & XFT_DBG_RENDER) {
		print("XftDisplayInfoGet failed to initialize, Xft unhappy :(\n");
	}
}

// Reduce memory usage in X server

static void _XftDisplayValidateMemory(void) {
	unsigned long glyph_memory = 0;
	for (XftFont* font=info.fonts; font; font=font->next) {
		glyph_memory += font->glyph_memory;
	}
	if (glyph_memory != info.glyph_memory)
		print("Display glyph cache incorrect has %ld bytes, should have %ld\n", info.glyph_memory, glyph_memory);
}

void _XftDisplayManageMemory(void) {
	if (!info.max_glyph_memory)
		return;
	
	if (XftDebug() & XFT_DBG_CACHE) {
		if (info.glyph_memory > info.max_glyph_memory)
			print("Reduce global memory from %ld to %ld\n",
			      info.glyph_memory, info.max_glyph_memory);
		_XftDisplayValidateMemory();
	}
	
	while (info.glyph_memory > info.max_glyph_memory) {
		unsigned long glyph_memory = rand() % info.glyph_memory;
		XftFont* font = info.fonts;
		while (font) {
			if (font->glyph_memory > glyph_memory) {
				_XftFontUncacheGlyph(font);
				break;
			}
			font = font->next;
			glyph_memory -= font->glyph_memory;
		}
	}
	if (XftDebug() & XFT_DBG_CACHE)
		_XftDisplayValidateMemory();
}
