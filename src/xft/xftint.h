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

typedef struct XftSolidColor {
	XRenderColor color;
	int screen; //we keep this because i think it uses -1 for invalid
	Picture pict;
} XftSolidColor;

#define XFT_NUM_SOLID_COLOR 16

typedef struct XftDisplayInfo {
	XftFont* fonts;
	size_t glyph_memory;
	size_t max_glyph_memory;
	XftSolidColor colors[XFT_NUM_SOLID_COLOR];
} XftDisplayInfo;

// By default, use no more than 4 meg of server memory total, and no
// more than 1 meg for any one font
#define XFT_FONT_MAX_GLYPH_MEMORY (1024 * 1024)

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
void xft_manage_memory(void);

/* xftfreetype.c */
FT_Face xft_lock_face(XftFont* pub);

/* xftglyph.c */
void xft_load_glyphs(XftFont* font, FT_UInt* glyphs, int nglyph);
void xft_font_uncache_glyph(XftFont* public);
void xft_font_manage_memory(XftFont* public);

/* xftinit.c */
void XftMemReport(void);
void XftMemAlloc(int kind, int size);
void* XftMalloc(int kind, size_t size);
void XftMemFree(int kind, int size);

extern XftDisplayInfo info;
