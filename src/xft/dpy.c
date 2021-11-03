#include "xftint.h"

XftDisplayInfo info = {
	.fonts = NULL
};

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
	
	//		print("XftDisplayInfoGet failed to initialize, Xft unhappy :(\n");
}
