#include "xftint.h"
#include <xcb/render.h>

struct XftDraw {
	Drawable drawable;
	xcb_render_picture_t pict;
};

static void XftDrawRenderPrepare(XftDraw* draw) {
	draw->pict = xcb_generate_id(W.c);
	xcb_render_create_picture_checked(W.c, draw->pict, draw->drawable, W.format->id, 0, NULL);
	
	if (!draw->pict)
		return; // fail!
}

XftDraw* XftDrawCreate(Drawable drawable) {
	XftDraw* draw = malloc(sizeof(XftDraw));
	
	*draw = (XftDraw){
		.drawable = drawable,
		.pict = 0,
	};
	XftMemAlloc(XFT_MEM_DRAW, sizeof(XftDraw));
	
	XftDrawRenderPrepare(draw);
	return draw;
}

void XftDrawChange(XftDraw* draw, Drawable drawable) {
	if (draw->pict)
		xcb_render_free_picture_checked(W.c, draw->pict);
	draw->drawable = drawable;
	XftDrawRenderPrepare(draw);
}

Drawable XftDrawDrawable(XftDraw* draw) {
	return draw->drawable;
}

void XftDrawDestroy(XftDraw* draw) {
	if (draw->pict)
		xcb_render_free_picture_checked(W.c, draw->pict);
	XftMemFree(XFT_MEM_DRAW, sizeof(XftDraw));
	free(draw);
}

xcb_render_picture_t XftDrawPicture(XftDraw* draw) {
	return draw->pict;
}

void XftDrawRect(XftDraw* draw, xcb_render_color_t color, int x, int y, unsigned int width, unsigned int height) {
	xcb_render_fill_rectangles_checked(W.c, XCB_RENDER_PICT_OP_SRC, draw->pict, color, 1, (xcb_rectangle_t[]){{x,y,width,height}});
}
