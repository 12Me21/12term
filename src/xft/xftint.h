#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "Xft.h"
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include "../x.h"

extern GlyphSet glyphset;
extern int glyphset_next;

#define XFT_NUM_SOLID_COLOR 16

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
bool load_glyph(XftFont* font, Char chr, GlyphData* out);
