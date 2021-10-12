#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xmd.h>
#include "Xft.h"
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include "../x.h"

/*
 * Internal version of the font with private data
 */

typedef enum XftClipType {
	XftClipTypeNone, XftClipTypeRegion, XftClipTypeRectangles
} XftClipType;

typedef struct XftClipRect {
	int xOrigin;
	int yOrigin;
	int n;
} XftClipRect;

#define XftClipRects(cr) ((XRectangle*) ((cr)+1))

typedef union XftClip {
	XftClipRect* rect;
	Region region;
} XftClip;

typedef struct XftSolidColor {
	XRenderColor color;
	int screen; //we keep this because i think it uses -1 for invalid
	Picture pict;
} XftSolidColor;

#define XFT_NUM_SOLID_COLOR 16

typedef struct XftDisplayInfo {
	XExtCodes* codes;
	FcPattern* defaults;
	XftFont* fonts;
	unsigned long glyph_memory;
	unsigned long max_glyph_memory;
	int num_unref_fonts;
	int max_unref_fonts;
	XftSolidColor colors[XFT_NUM_SOLID_COLOR];
} XftDisplayInfo;

// By default, use no more than 4 meg of server memory total, and no
// more than 1 meg for any one font
#define XFT_DPY_MAX_GLYPH_MEMORY (4 * 1024 * 1024)
#define XFT_FONT_MAX_GLYPH_MEMORY (1024 * 1024)

// By default, keep the last 16 unreferenced fonts around to
// speed reopening them.  Note that the glyph caching code
// will keep the global memory usage reasonably limited
//  no
#define XFT_DPY_MAX_UNREF_FONTS 0

#define XFT_DBG_OPEN	1
#define XFT_DBG_OPENV 2
#define XFT_DBG_RENDER 4
#define XFT_DBG_DRAW	8
#define XFT_DBG_REF 16
#define XFT_DBG_GLYPH 32
#define XFT_DBG_GLYPHV 64
#define XFT_DBG_CACHE 128
#define XFT_DBG_CACHEV 256
#define XFT_DBG_MEMORY 512

#define XFT_MEM_DRAW	0
#define XFT_MEM_FONT	1
#define XFT_MEM_FILE	2
#define XFT_MEM_GLYPH 3
#define XFT_MEM_NUM 4

/* xftdbg.c */
int XftDebug(void);

/* xftdpy.c */
void _XftDisplayManageMemory(void);

/* xftfreetype.c */
void XftFontManageMemory(void);

/* xftglyph.c */
void _XftFontUncacheGlyph(XftFont* public);
void _XftFontManageMemory(XftFont* public);

/* xftinit.c */
void XftMemReport(void);
void XftMemAlloc(int kind, int size);
void* XftMalloc(int kind, size_t size);
void XftMemFree(int kind, int size);

/* xftswap.c */
int XftNativeByteOrder(void);
void XftSwapCARD32(CARD32* data, int n);
void XftSwapImage(XImage* image);

extern XftDisplayInfo info;
