#include "xftint.h"

_X_HIDDEN int XftNativeByteOrder(void) {
	int whichbyte = 1;
	if (*((char*)&whichbyte))
		return LSBFirst;
	return MSBFirst;
}

// UNTESTED!

// this is also used in xftglyphs.c
void XftSwapCARD32(uint32_t* data, int u) {
	while (u--) {
		*data = (*data>>24 & 0xFF) | (*data>>16 & 0xFF)<<8 | (*data>>8 & 0xFF)<<16 | (*data & 0xFF)<<24;
		data++;
	}
}

static void XftSwapCARD24(uint8_t* data, int width, int height) {
	int units = width/3;
	while (height--) {
		uint8_t* d = data;
		data += width;
		int u = units;
		while (u--) {
			uint8_t temp = d[0];
			d[0] = d[3];
			d[3] = temp;
			d += 3;
		}
	}
}

static void XftSwapCARD16(uint16_t* data, int u) {
	while (u--) {
		*data = (*data>>8 & 0xFF) | (*data & 0xFF)<<8;
		data++;
	}
}

_X_HIDDEN void XftSwapImage(XImage* image) {
	switch (image->bits_per_pixel) {
	case 32:
		XftSwapCARD32((uint32_t*)image->data, image->height*image->bytes_per_line/4);
		break;
	case 24:
		XftSwapCARD24((uint8_t*)image->data, image->bytes_per_line, image->height);
		break;
	case 16:
		XftSwapCARD16((uint16_t*)image->data, image->height*image->bytes_per_line/2);
		break;
	default:
		break;
	}
}
