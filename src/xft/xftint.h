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

extern GlyphSet glyphset;
extern int glyphset_next;

typedef struct XftSolidColor {
	XRenderColor color;
	int screen; //we keep this because i think it uses -1 for invalid
	Picture pict;
} XftSolidColor;

#define XFT_NUM_SOLID_COLOR 16

typedef struct XftDisplayInfo {
	XftFont* fonts;
	XftSolidColor colors[XFT_NUM_SOLID_COLOR];
} XftDisplayInfo;

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

/* xftdbg.c */
int XftDebug(void);

/* xftfreetype.c */
FT_Face xft_lock_face(XftFont* pub);

/* xftglyph.c */
bool xft_load_glyphs(XftFont* font, const FT_UInt* glyphs, int nglyph);
bool load_glyph(XftFont* font, Char chr, GlyphData* out);

extern XftDisplayInfo info;
