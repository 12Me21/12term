#include "xftint.h"

_X_EXPORT Bool
XftColorAllocName (_Xconst Visual   *visual,
                   Colormap cmap,
                   _Xconst char	    *name,
                   XftColor *result)
{
	XColor  screen, exact;

	if (!XAllocNamedColor (W.d, cmap, name, &screen, &exact)) {
		/* XXX stick standard colormap stuff here */
		return False;
	}

	result->pixel = screen.pixel;
	result->color.red = exact.red;
	result->color.green = exact.green;
	result->color.blue = exact.blue;
	result->color.alpha = 0xffff;
	return True;
}

static short maskbase (unsigned long m) {
	if (!m)
		return 0;
	short i = 0;
	while (!(m&1)) {
		m>>=1;
		i++;
	}
	return i;
}

static short masklen (unsigned long m) {
	unsigned long y;
	y = (m >> 1) &033333333333;
	y = m - y - ((y >>1) & 033333333333);
	return (short) (((y + (y >> 3)) & 030707070707) % 077);
}

_X_EXPORT Bool
XftColorAllocValue (Visual	    *visual,
                    Colormap	    cmap,
                    _Xconst XRenderColor    *color,
                    XftColor	    *result) {
	int red_shift = maskbase(visual->red_mask);
	int red_len = masklen(visual->red_mask);
	int green_shift = maskbase(visual->green_mask);
	int green_len = masklen(visual->green_mask);
	int blue_shift = maskbase(visual->blue_mask);
	int blue_len = masklen(visual->blue_mask);
	result->pixel = (((color->red >> (16 - red_len)) << red_shift) |
	                 ((color->green >> (16 - green_len)) << green_shift) |
	                 ((color->blue >> (16 - blue_len)) << blue_shift));
	result->color.red = color->red;
	result->color.green = color->green;
	result->color.blue = color->blue;
	result->color.alpha = color->alpha;
	return True;
}
