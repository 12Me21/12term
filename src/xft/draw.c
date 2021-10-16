#include "xftint.h"

// really this is just a helper function for creating a Picture given a width/height. this can be absorbed into draw.c soon

struct XftDraw {
	Drawable drawable;
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
	free(draw);
}

void XftDrawPut(XftDraw* draw, Px x, Px y, Px w, Px h, Px dx, Px dy) {
	XCopyArea(W.d, draw->drawable, W.win, W.gc, x, y, w, h, dx, dy);
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
		return true;
	
	if (r && draw->clip_type == XftClipTypeRegion &&
	    XEqualRegion(r, draw->clip.region)) {
		return true;
	}
	
	// Duplicate the region so future changes can be short circuited
	if (r) {
		n = XCreateRegion();
		if (n) {
			if (!XUnionRegion(n, r, n)) {
				XDestroyRegion(n);
				return false;
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
	return true;
	}*/

/*bool XftDrawSetClipRectangles(XftDraw* draw, int xOrigin, int yOrigin, const XRectangle* rects, int n) {
	XftClipRect	*new = NULL;
	
	// Check for quick exit
	if (draw->clip_type == XftClipTypeRectangles &&
	    draw->clip.rect->n == n &&
	    (n == 0 || (draw->clip.rect->xOrigin == xOrigin &&
	                draw->clip.rect->yOrigin == yOrigin)) &&
	    !memcmp (XftClipRects(draw->clip.rect), rects, n * sizeof (XRectangle))) {
		return true;
	}
	
	// Duplicate the region so future changes can be short circuited
	new = malloc(sizeof(XftClipRect)+ n*sizeof(XRectangle));
	if (!new)
		return false;
	
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
	return true;
}
*/
