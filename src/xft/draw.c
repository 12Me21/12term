#include "xftint.h"

struct XftDraw {
	Drawable drawable;
	//XftClipType clip_type;
	//XftClip clip;
	//int subwindow_mode;
	// this controlled the Picture's .subwindow_mode field but I'm not sure what that does
	Picture pict;
};

XftDraw* XftDrawCreate(Px w, Px h) {
	XftDraw* draw = malloc(sizeof(XftDraw));
	draw->drawable = XCreatePixmap(W.d, W.win, w, h, DefaultDepth(W.d, W.scr));
	draw->pict = XRenderCreatePicture(W.d, draw->drawable, W.format, 0, NULL);
	return draw;
}

void XftDrawDestroy(XftDraw* draw) {
	XFreePixmap(W.d, draw->drawable);
	XRenderFreePicture(W.d, draw->pict);
	/*switch (draw->clip_type) {
	case XftClipTypeRegion:
		XDestroyRegion(draw->clip.region);
		break;
	case XftClipTypeRectangles:
		free(draw->clip.rect);
		break;
	case XftClipTypeNone:
		break;
		}*/
	free(draw);
}

void XftDrawPut(XftDraw* draw, Px x, Px y, Px w, Px h, Px dx, Px dy) {
	XCopyArea(W.d, draw->drawable, W.win, W.gc, x, y, w, h, dx, dy);
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

Picture XftDrawPicture(XftDraw* draw) {
	return draw->pict;
}

void XftDrawRect(XftDraw* draw, const XRenderColor color, Px x, Px y, Px width, Px height) {
	XRenderFillRectangle(W.d, PictOpSrc, draw->pict, &color, x, y, width, height);
}

/*bool XftDrawSetClip(XftDraw* draw, Region r) {
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
		free(draw->clip.rect);
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
	}*/

/*bool XftDrawSetClipRectangles(XftDraw* draw, int xOrigin, int yOrigin, const XRectangle* rects, int n) {
	XftClipRect	*new = NULL;
	
	// Check for quick exit
	if (draw->clip_type == XftClipTypeRectangles &&
	    draw->clip.rect->n == n &&
	    (n == 0 || (draw->clip.rect->xOrigin == xOrigin &&
	                draw->clip.rect->yOrigin == yOrigin)) &&
	    !memcmp (XftClipRects(draw->clip.rect), rects, n * sizeof (XRectangle))) {
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
*/
