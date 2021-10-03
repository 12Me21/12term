#include "xftint.h"

unsigned int XftDrawDepth(XftDraw* draw) {
	if (!draw->depth) {
		Window root;
		int x, y;
		unsigned int width, height, borderWidth, depth;
		if (XGetGeometry(W.d, draw->drawable, &root, &x, &y, &width, &height, &borderWidth, &depth))
			draw->depth = depth;
	}
	return draw->depth;
}

unsigned int XftDrawBitsPerPixel(XftDraw *draw) {
	if (!draw->bits_per_pixel) {
		int nformats;
		XPixmapFormatValues* formats = XListPixmapFormats(W.d, &nformats);
		unsigned int depth = XftDrawDepth(draw);
		if (depth && formats) {
			for (int i=0; i<nformats; i++) {
				if (formats[i].depth == depth) {
					draw->bits_per_pixel = formats[i].bits_per_pixel;
					break;
				}
			}
			XFree(formats);
		}
	}
	return draw->bits_per_pixel;
}

XftDraw* XftDrawCreate(Drawable drawable, Visual* visual, Colormap colormap) {
	XftDraw* draw = malloc(sizeof(XftDraw));
	
	if (!draw)
		return NULL;
	
	*draw = (XftDraw){
		.drawable = drawable,
		.depth = 0,
		.bits_per_pixel = 0,
		.visual = visual,
		.colormap = colormap,
		.pict = 0,
		.clip_type = XftClipTypeNone,
		.subwindow_mode = ClipByChildren,
	};
	XftMemAlloc(XFT_MEM_DRAW, sizeof(XftDraw));
	return draw;
}

static XRenderPictFormat* _XftDrawFormat(XftDraw* draw) {
	XftDisplayInfo* info = _XftDisplayInfoGet(True);
	
	if (!info)
		return NULL;
	
	if (draw->visual == NULL) {
		unsigned int depth = XftDrawDepth(draw);
		return XRenderFindFormat(
			W.d,
			PictFormatType | PictFormatDepth | PictFormatAlpha | PictFormatAlphaMask,
			&(XRenderPictFormat){
				.type = PictTypeDirect,
				.depth = depth,
				.direct = {
					.alpha = 0,
					.alphaMask = (1<<depth)-1,
				},
			},
			0);
	} else
		return XRenderFindVisualFormat(W.d, draw->visual);
}

void XftDrawChange(XftDraw* draw, Drawable drawable) {
	draw->drawable = drawable;
	if (draw->pict) {
		XRenderFreePicture (W.d, draw->pict);
		draw->pict = 0;
	}
}

Display* XftDrawDisplay(XftDraw* draw) {
	return W.d;
}

Drawable XftDrawDrawable(XftDraw* draw) {
	return draw->drawable;
}

Colormap XftDrawColormap(XftDraw* draw) {
	return draw->colormap;
}

Visual* XftDrawVisual(XftDraw* draw) {
	return draw->visual;
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

// i should use this more perhaps
Picture XftDrawSrcPicture(XftDraw* draw, const XftColor* color) {
	XftDisplayInfo* info = _XftDisplayInfoGet(true);
	
	if (!info || !info->solidFormat)
		return 0;
	
	XftColor bitmapColor;
	// Monochrome targets require special handling; the PictOp controls
	// the color, and the color must be opaque
	if (!draw->visual && draw->depth==1) {
		bitmapColor.color.alpha = 0xffff;
		bitmapColor.color.red   = 0xffff;
		bitmapColor.color.green = 0xffff;
		bitmapColor.color.blue  = 0xffff;
		color = &bitmapColor;
	}

	// See if there's one already available
	for (int i=0; i<XFT_NUM_SOLID_COLOR; i++) {
		if (info->colors[i].pict &&
		    info->colors[i].screen == W.scr &&
		    !memcmp(&color->color, &info->colors[i].color, sizeof(XRenderColor)))
			return info->colors[i].pict;
	}
	// Pick one to replace at random
	int i = (unsigned int)rand() % XFT_NUM_SOLID_COLOR;

	if (info->hasSolid) {
		// Free any existing entry
		if (info->colors[i].pict)
			XRenderFreePicture(W.d, info->colors[i].pict);
		// Create picture
		info->colors[i].pict = XRenderCreateSolidFill(W.d, &color->color);
	} else {
		if (info->colors[i].screen!=W.scr && info->colors[i].pict) {
			XRenderFreePicture(W.d, info->colors[i].pict);
			info->colors[i].pict = 0;
		}
		// Create picture if necessary
		if (!info->colors[i].pict) {
			Pixmap pix = XCreatePixmap(
				W.d, RootWindow(W.d, W.scr),
				1, 1, info->solidFormat->depth);
			info->colors[i].pict = XRenderCreatePicture(
				W.d, pix,
				info->solidFormat,
				CPRepeat, &(XRenderPictureAttributes){.repeat=True});
			XFreePixmap(W.d, pix);
		}
		// Set to the new color
		info->colors[i].color = color->color;
		info->colors[i].screen = W.scr;
		XRenderFillRectangle(W.d, PictOpSrc, info->colors[i].pict, &color->color, 0, 0, 1, 1);
	}
	info->colors[i].color = color->color;
	info->colors[i].screen = W.scr;

	return info->colors[i].pict;
}

static FcBool _XftDrawRenderPrepare (XftDraw* draw) {
	if (!draw->pict) {
		XRenderPictFormat* format = _XftDrawFormat(draw);
		if (!format)
			return FcFalse;
		
		unsigned long mask = 0;
		XRenderPictureAttributes pa;
		if (draw->subwindow_mode == IncludeInferiors) {
			pa.subwindow_mode = IncludeInferiors;
			mask |= CPSubwindowMode;
		}
		draw->pict = XRenderCreatePicture(W.d, draw->drawable, format, mask, &pa);
		
		if (!draw->pict)
			return FcFalse;
		switch (draw->clip_type) {
		case XftClipTypeRegion:
			XRenderSetPictureClipRegion(W.d, draw->pict, draw->clip.region);
			break;
		case XftClipTypeRectangles:
			XRenderSetPictureClipRectangles (W.d, draw->pict,
			                                 draw->clip.rect->xOrigin,
			                                 draw->clip.rect->yOrigin,
			                                 XftClipRects(draw->clip.rect),
			                                 draw->clip.rect->n);
			break;
		case XftClipTypeNone:
			break;
		}
	}
	return FcTrue;
}

Picture XftDrawPicture(XftDraw* draw) {
	if (!_XftDrawRenderPrepare(draw))
		return 0;
	return draw->pict;
}

void XftDrawRect(XftDraw* draw, const XftColor* color, int x, int y, unsigned int width, unsigned int height) {
	if (_XftDrawRenderPrepare(draw)) {
		XRenderFillRectangle(
			W.d, PictOpSrc, draw->pict,
			&color->color, x, y, width, height);
	}
}

Bool XftDrawSetClip(XftDraw* draw, Region r) {
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
		n = XCreateRegion ();
		if (n) {
			if (!XUnionRegion (n, r, n)) {
				XDestroyRegion (n);
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

Bool XftDrawSetClipRectangles(XftDraw* draw, int xOrigin, int yOrigin, const XRectangle* rects, int n) {
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

void XftDrawSetSubwindowMode (XftDraw *draw, int mode) {
	if (mode == draw->subwindow_mode)
		return;
	draw->subwindow_mode = mode;
	if (draw->pict) {
		XRenderChangePicture(W.d, draw->pict, CPSubwindowMode, &(XRenderPictureAttributes){
			.subwindow_mode = mode,
		});
	}
}
