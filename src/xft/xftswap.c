#include "xftint.h"

_X_HIDDEN int XftNativeByteOrder(void) {
	int whichbyte = 1;
	if (*((char*)&whichbyte))
		return LSBFirst;
	return MSBFirst;
}

// this is gross

/* byte swap a 32-bit value */
#define swapl(x, n) {	  \
		n = ((char *) (x))[0]; \
		((char *) (x))[0] = ((char *) (x))[3]; \
		((char *) (x))[3] = n; \
		n = ((char *) (x))[1]; \
		((char *) (x))[1] = ((char *) (x))[2]; \
		((char *) (x))[2] = n; }

/* byte swap a short */
#define swaps(x, n) {	  \
		n = ((char *) (x))[0]; \
		((char *) (x))[0] = ((char *) (x))[1]; \
		((char *) (x))[1] = n; }

/* byte swap a three-byte unit */
#define swapt(x, n) {	  \
		n = ((char *) (x))[0]; \
		((char *) (x))[0] = ((char *) (x))[2]; \
		((char *) (x))[2] = n; }

_X_HIDDEN void XftSwapCARD32(CARD32* data, int u) {
	while (u--) {
		char n;
		swapl(data, n);
		data++;
	}
}

_X_HIDDEN void XftSwapCARD24(CARD8* data, int width, int height) {
	int units = width / 3;
	while (height--) {
		CARD8* d = data;
		data += width;
		int u = units;
		while (u--) {
			char n;
			swapt(d, n);
			d += 3;
		}
	}
}

_X_HIDDEN void XftSwapCARD16 (CARD16* data, int u) {
	while (u--) {
		char n;
		swaps (data, n);
		data++;
	}
}

_X_HIDDEN void XftSwapImage(XImage* image) {
	switch (image->bits_per_pixel) {
	case 32:
		XftSwapCARD32((CARD32*)image->data,
		              image->height * image->bytes_per_line >> 2);
		break;
	case 24:
		XftSwapCARD24((CARD8*)image->data,
		              image->bytes_per_line,
		              image->height);
		break;
	case 16:
		XftSwapCARD16((CARD16*)image->data,
		              image->height * image->bytes_per_line >> 1);
		break;
	default:
		break;
	}
}
