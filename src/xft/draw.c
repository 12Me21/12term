#include "xftint.h"

struct XftDraw {
	Drawable drawable;
	XftClipType clip_type;
	XftClip clip;
	int subwindow_mode;
	Picture pict;
};

static void XftDrawRenderPrepare(XftDraw* draw) {
	unsigned long mask = 0;
	XRenderPictureAttributes pa;
	if (draw->subwindow_mode == IncludeInferiors) {
		pa.subwindow_mode = IncludeInferiors;
		mask |= CPSubwindowMode;
	}
	draw->pict = XRenderCreatePicture(W.d, draw->drawable, W.format, mask, &pa);
		
	if (!draw->pict)
		return; // fail!
	switch (draw->clip_type) {
	case XftClipTypeRegion:
		XRenderSetPictureClipRegion(W.d, draw->pict, draw->clip.region);
		break;
	case XftClipTypeRectangles:
		XRenderSetPictureClipRectangles(W.d, draw->pict, draw->clip.rect->xOrigin,
		                                draw->clip.rect->yOrigin,
		                                XftClipRects(draw->clip.rect),
		                                draw->clip.rect->n);
		break;
	case XftClipTypeNone:
		break;
	}
}

XftDraw* XftDrawCreate(Drawable drawable) {
	XftDraw* draw = malloc(sizeof(XftDraw));
	
	*draw = (XftDraw){
		.drawable = drawable,
		.pict = 0,
		.clip_type = XftClipTypeNone,
		.subwindow_mode = ClipByChildren,
	};
	XftMemAlloc(XFT_MEM_DRAW, sizeof(XftDraw));
	
	XftDrawRenderPrepare(draw);
	return draw;
}

void XftDrawChange(XftDraw* draw, Drawable drawable) {
	if (draw->pict)
		XRenderFreePicture(W.d, draw->pict);
	draw->drawable = drawable;
	XftDrawRenderPrepare(draw);
}

Drawable XftDrawDrawable(XftDraw* draw) {
	return draw->drawable;
}

void XftDrawDestroy(XftDraw* draw) {
	if (draw->pict)
		XRenderFreePicture(W.d, draw->pict);
	switch (draw->clip_type) {
	case XftClipTypeRegion:
		XDestroyRegion(draw->clip.region);
		break;
	case XftClipTypeRectangles:
		free(draw->clip.rect);
		break;
	case XftClipTypeNone:
		break;
	}
	XftMemFree(XFT_MEM_DRAW, sizeof(XftDraw));
	free(draw);
}

static Picture pict;

// improve caching here
Picture XftDrawSrcPicture(const XRenderColor* color) {
	/*if (pict)
		XRenderFreePicture(W.d, pict);
		return pict =XRenderCreateSolidFill(W.d, color);*/
	
	// See if there's one already available
	for (int i=0; i<XFT_NUM_SOLID_COLOR; i++) {
		if (info.colors[i].pict && info.colors[i].screen == W.scr && !memcmp(color, &info.colors[i].color, sizeof(XRenderColor)))
			return info.colors[i].pict;
	}
	// Pick one to replace at random
	int i = (unsigned int)rand() % XFT_NUM_SOLID_COLOR;
	
	// Free any existing entry
	if (info.colors[i].pict)
		XRenderFreePicture(W.d, info.colors[i].pict);
	// Create picture
	// is it worth caching this anymore?
	info.colors[i].pict = XRenderCreateSolidFill(W.d, color);
	
	info.colors[i].color = *color;
	info.colors[i].screen = W.scr;

	return info.colors[i].pict;
}

Picture XftDrawPicture(XftDraw* draw) {
	return draw->pict;
}

void XftDrawRect(XftDraw* draw, const XRenderColor* color, int x, int y, unsigned int width, unsigned int height) {
	XRenderFillRectangle(W.d, PictOpSrc, draw->pict, color, x, y, width, height);
}

bool XftDrawSetClip(XftDraw* draw, Region r) {
	Region n = NULL;
	
	// Check for quick exits
	if (!r && draw->clip_type == XftClipTypeNone)
		return True;
	
	if (r && draw->clip_type == XftClipTypeRegion &&
	    XEqualRegion(r, draw->clip.region)) {
		return True;
	}
	
	// Duplicate the region so future changes can be short circuited
	if (r) {
		n = XCreateRegion();
		if (n) {
			if (!XUnionRegion(n, r, n)) {
				XDestroyRegion(n);
				return False;
			}
		}
	}

	// Destroy existing clip
	switch (draw->clip_type) {
	case XftClipTypeRegion:
		XDestroyRegion (draw->clip.region);
		break;
	case XftClipTypeRectangles:
		free (draw->clip.rect);
		break;
	case XftClipTypeNone:
		break;
	}
	
	// Set the clip
	if (n) {
		draw->clip_type = XftClipTypeRegion;
		draw->clip.region = n;
	} else {
		draw->clip_type = XftClipTypeNone;
	}
	// Apply new clip to existing objects
	if (draw->pict) {
		if (n)
			XRenderSetPictureClipRegion(W.d, draw->pict, n);
		else {
			XRenderChangePicture(W.d, draw->pict,
			                     CPClipMask, &(XRenderPictureAttributes){
				                     .clip_mask = None,
			                     });
		}
	}
	return True;
}

bool XftDrawSetClipRectangles(XftDraw* draw, int xOrigin, int yOrigin, const XRectangle* rects, int n) {
	XftClipRect	*new = NULL;
	
	// Check for quick exit
	if (draw->clip_type == XftClipTypeRectangles &&
	    draw->clip.rect->n == n &&
	    (n == 0 || (draw->clip.rect->xOrigin == xOrigin &&
	                draw->clip.rect->yOrigin == yOrigin)) &&
	    !memcmp (XftClipRects (draw->clip.rect), rects, n * sizeof (XRectangle))) {
		return True;
	}
	
	// Duplicate the region so future changes can be short circuited
	new = malloc(sizeof(XftClipRect)+ n*sizeof(XRectangle));
	if (!new)
		return False;
	
	new->n = n;
	new->xOrigin = xOrigin;
	new->yOrigin = yOrigin;
	memcpy(XftClipRects(new), rects, n*sizeof(XRectangle));
	
	// Destroy existing clip
	switch (draw->clip_type) {
	case XftClipTypeRegion:
		XDestroyRegion (draw->clip.region);
		break;
	case XftClipTypeRectangles:
		free (draw->clip.rect);
		break;
	case XftClipTypeNone:
		break;
	}
	
	// Set the clip
	draw->clip_type = XftClipTypeRectangles;
	draw->clip.rect = new;
	// Apply new clip to existing objects
	if (draw->pict) {
		XRenderSetPictureClipRectangles(
			W.d, draw->pict,
			new->xOrigin, new->yOrigin,
			XftClipRects(new),
			new->n);
	}
	return True;
}

void XftDrawSetSubwindowMode(XftDraw* draw, int mode) {
	if (mode == draw->subwindow_mode)
		return;
	draw->subwindow_mode = mode;
	if (draw->pict) {
		XRenderChangePicture(W.d, draw->pict, CPSubwindowMode, &(XRenderPictureAttributes){
			.subwindow_mode = mode,
		});
	}
}
