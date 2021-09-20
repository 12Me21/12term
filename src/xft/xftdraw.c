#include "xftint.h"

/*
 * Ok, this is a pain.  To share source pictures across multiple destinations,
 * the screen for each drawable must be discovered.
 */
static int _XftDrawScreen(Display* dpy, Drawable drawable, Visual* visual) {
	/* Special case the most common environment */
	if (ScreenCount(dpy)==1)
		return 0;
	// If we've got a visual, look for the screen that points at it.
	// This requires no round trip.
	if (visual) {
		for (int s=0; s < ScreenCount(dpy); s++) {
			XVisualInfo	template, *ret;
			int nret;
			
			template.visualid = visual->visualid;
			template.screen = s;
			ret = XGetVisualInfo(dpy, VisualIDMask|VisualScreenMask, &template, &nret);
			if (ret) {
				XFree (ret);
				return s;
			}
		}
	}
	/*
	 * Otherwise, ask the server for the drawable geometry and find
	 * the screen from the root window.
	 * This takes a round trip.
	 */
	Window root;
	int x, y; // ignore all these
	unsigned int width, height, borderWidth, depth;
	if (XGetGeometry(dpy, drawable, &root, &x, &y, &width, &height, &borderWidth, &depth)) {
		for (int s=0; s<ScreenCount(dpy); s++) {
			if (RootWindow(dpy, s) == root)
				return s;
		}
	}
	/*
	 * Make a guess -- it's probably wrong, but then the app probably
	 * handed us a bogus drawable in this case
	 */
	return 0;
}

unsigned int XftDrawDepth(XftDraw* draw) {
	if (!draw->depth) {
		Window root;
		int x, y;
		unsigned int width, height, borderWidth, depth;
		if (XGetGeometry(draw->dpy, draw->drawable,
		                 &root, &x, &y, &width, &height,
		                 &borderWidth, &depth))
			draw->depth = depth;
	}
	return draw->depth;
}

unsigned int XftDrawBitsPerPixel(XftDraw *draw) {
	if (!draw->bits_per_pixel) {
		int nformats;
		XPixmapFormatValues* formats = XListPixmapFormats(draw->dpy, &nformats);
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

XftDraw* XftDrawCreate(Display* dpy, Drawable drawable, Visual* visual, Colormap colormap) {
	XftDraw* draw = malloc(sizeof(XftDraw));
	
	if (!draw)
		return NULL;
	
	*draw = (XftDraw){
		.dpy = dpy,
		.drawable = drawable,
		.screen = _XftDrawScreen (dpy, drawable, visual),
		.depth = 0,	/* don't find out unless we need to know */
		.bits_per_pixel = 0,	/* don't find out unless we need to know */
		.visual = visual,
		.colormap = colormap,
		.render.pict = 0,
		.clip_type = XftClipTypeNone,
		.subwindow_mode = ClipByChildren,
	};
	XftMemAlloc(XFT_MEM_DRAW, sizeof(XftDraw));
	return draw;
}

XftDraw* XftDrawCreateBitmap(Display *dpy, Pixmap bitmap) {
	XftDraw* draw = XftMalloc(XFT_MEM_DRAW, sizeof(XftDraw));
	if (!draw)
		return NULL;
	draw->dpy = dpy;
	draw->drawable = (Drawable)bitmap;
	draw->screen = _XftDrawScreen(dpy, bitmap, NULL);
	draw->depth = 1;
	draw->bits_per_pixel = 1;
	draw->visual = NULL;
	draw->colormap = 0;
	draw->render.pict = 0;
	draw->clip_type = XftClipTypeNone;
	draw->subwindow_mode = ClipByChildren;
	return draw;
}

XftDraw* XftDrawCreateAlpha(Display* dpy, Pixmap pixmap, int depth) {
	XftDraw* draw = XftMalloc(XFT_MEM_DRAW, sizeof(XftDraw));
	if (!draw)
		return NULL;
	draw->dpy = dpy;
	draw->drawable = (Drawable)pixmap;
	draw->screen = _XftDrawScreen(dpy, pixmap, NULL);
	draw->depth = depth;
	draw->bits_per_pixel = 0;	/* don't find out until we need it */
	draw->visual = NULL;
	draw->colormap = 0;
	draw->render.pict = 0;
	draw->clip_type = XftClipTypeNone;
	draw->subwindow_mode = ClipByChildren;
	return draw;
}

static XRenderPictFormat* _XftDrawFormat(XftDraw* draw) {
	XftDisplayInfo* info = _XftDisplayInfoGet(draw->dpy, True);
	
	if (!info)
		return NULL;
	
	if (draw->visual == NULL) {
		unsigned int depth = XftDrawDepth(draw);
		return XRenderFindFormat(
			draw->dpy,
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
		return XRenderFindVisualFormat(draw->dpy, draw->visual);
}

void XftDrawChange(XftDraw* draw, Drawable drawable) {
	draw->drawable = drawable;
	if (draw->render.pict) {
		XRenderFreePicture (draw->dpy, draw->render.pict);
		draw->render.pict = 0;
	}
}

Display* XftDrawDisplay(XftDraw* draw) {
	return draw->dpy;
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
	if (draw->render.pict)
		XRenderFreePicture(draw->dpy, draw->render.pict);
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
	Display* dpy = draw->dpy;
	XftDisplayInfo* info = _XftDisplayInfoGet(dpy, True);
	
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
		    info->colors[i].screen == draw->screen &&
		    !memcmp(&color->color, &info->colors[i].color, sizeof(XRenderColor)))
			return info->colors[i].pict;
	}
	// Pick one to replace at random
	int i = (unsigned int)rand() % XFT_NUM_SOLID_COLOR;

	if (info->hasSolid) {
		// Free any existing entry
		if (info->colors[i].pict)
			XRenderFreePicture(dpy, info->colors[i].pict);
		// Create picture
		info->colors[i].pict = XRenderCreateSolidFill(draw->dpy, &color->color);
	} else {
		if (info->colors[i].screen!=draw->screen && info->colors[i].pict) {
			XRenderFreePicture(dpy, info->colors[i].pict);
			info->colors[i].pict = 0;
		}
		// Create picture if necessary
		if (!info->colors[i].pict) {
			Pixmap pix = XCreatePixmap(
				dpy, RootWindow(dpy, draw->screen),
				1, 1, info->solidFormat->depth);
			info->colors[i].pict = XRenderCreatePicture(
				draw->dpy, pix,
				info->solidFormat,
				CPRepeat, &(XRenderPictureAttributes){.repeat=True});
			XFreePixmap(dpy, pix);
		}
		// Set to the new color
		info->colors[i].color = color->color;
		info->colors[i].screen = draw->screen;
		XRenderFillRectangle(dpy, PictOpSrc, info->colors[i].pict, &color->color, 0, 0, 1, 1);
	}
	info->colors[i].color = color->color;
	info->colors[i].screen = draw->screen;

	return info->colors[i].pict;
}

static FcBool _XftDrawRenderPrepare (XftDraw* draw) {
	if (!draw->render.pict) {
		XRenderPictFormat* format = _XftDrawFormat(draw);
		if (!format)
			return FcFalse;
		
		unsigned long mask = 0;
		XRenderPictureAttributes pa;
		if (draw->subwindow_mode == IncludeInferiors) {
			pa.subwindow_mode = IncludeInferiors;
			mask |= CPSubwindowMode;
		}
		draw->render.pict = XRenderCreatePicture(draw->dpy, draw->drawable, format, mask, &pa);
		
		if (!draw->render.pict)
			return FcFalse;
		switch (draw->clip_type) {
		case XftClipTypeRegion:
			XRenderSetPictureClipRegion (draw->dpy, draw->render.pict,
			                             draw->clip.region);
			break;
		case XftClipTypeRectangles:
			XRenderSetPictureClipRectangles (draw->dpy, draw->render.pict,
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
	return draw->render.pict;
}

#define NUM_LOCAL   1024

void XftDrawRect(XftDraw* draw, const XftColor* color, int x, int y, unsigned int width, unsigned int height) {
	if (_XftDrawRenderPrepare(draw)) {
		XRenderFillRectangle(
			draw->dpy, PictOpSrc, draw->render.pict,
			&color->color, x, y, width, height);
	}
}

Bool XftDrawSetClip(XftDraw* draw, Region r) {
	Region n = NULL;
	
	// Check for quick exits
	if (!r && draw->clip_type == XftClipTypeNone)
		return True;
	
	if (r &&
	    draw->clip_type == XftClipTypeRegion &&
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
	if (draw->render.pict) {
		if (n)
			XRenderSetPictureClipRegion(draw->dpy, draw->render.pict, n);
		else {
			XRenderChangePicture(draw->dpy, draw->render.pict,
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
	if (draw->render.pict) {
		XRenderSetPictureClipRectangles(
			draw->dpy, draw->render.pict,
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
	if (draw->render.pict) {
		XRenderChangePicture(draw->dpy, draw->render.pict, CPSubwindowMode, &(XRenderPictureAttributes){
			.subwindow_mode = mode,
		});
	}
}
