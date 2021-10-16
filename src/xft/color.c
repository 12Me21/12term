#include <X11/extensions/Xrender.h>
#include "../x.h"
#include "Xft.h"
#include "xftint.h"

static short maskbase(unsigned long m) {
	if (!m)
		return 0;
	short i = 0;
	while (!(m&1)) {
		m>>=1;
		i++;
	}
	return i;
}

static short masklen(unsigned long m) {
	unsigned long y;
	y = (m>>1) & 033333333333;
	y = m - y - ((y>>1) & 033333333333);
	return (short)(((y + (y>>3)) & 030707070707) % 077);
}

unsigned long XftColorAllocValue(const XRenderColor* color) {
	int red_shift = maskbase(W.vis->red_mask);
	int red_len = masklen(W.vis->red_mask);
	int green_shift = maskbase(W.vis->green_mask);
	int green_len = masklen(W.vis->green_mask);
	int blue_shift = maskbase(W.vis->blue_mask);
	int blue_len = masklen(W.vis->blue_mask);
	return ((color->red >> 16-red_len) << red_shift) |
		((color->green >> 16-green_len) << green_shift) |
		((color->blue >> 16-blue_len) << blue_shift);
}

// improve caching here
Picture XftDrawSrcPicture(const XRenderColor color) {
	/*if (pict)
		XRenderFreePicture(W.d, pict);
		return pict =XRenderCreateSolidFill(W.d, color);*/
	
	// See if there's one already available
	for (int i=0; i<XFT_NUM_SOLID_COLOR; i++) {
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
