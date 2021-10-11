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
 * Glyphs are stored in this structure
 */
typedef struct XftGlyph {
	XGlyphInfo metrics;
	void* bitmap;
	unsigned long glyph_memory;
	Picture picture;
} XftGlyph;

/*
 * A hash table translates Unicode values into glyph indicies
 */
typedef struct XftUcsHash {
	FcChar32 ucs4;
	FT_UInt glyph;
} XftUcsHash;

/*
 * Many fonts can share the same underlying face data; this
 * structure references that.  Note that many faces may in fact
 * live in the same font file; that is irrelevant to this structure
 * which is concerned only with the individual faces themselves
 */
typedef struct XftFtFile {
	struct XftFtFile	*next;
	int ref; // number of font infos using this file
	
	char* file;	// file name
	int id; // font index within that file
	
	FT_F26Dot6 xsize;	// current xsize setting
	FT_F26Dot6 ysize;	// current ysize setting
	FT_Matrix matrix;	// current matrix setting
	
	int lock; // lock count; can't unload unless 0
	FT_Face face; // pointer to face; only valid when lock
} XftFtFile;

/*
 * This structure holds the data extracted from a pattern
 * needed to create a unique font object.
 */
struct XftFontInfo {
	// Hash value (not include in hash value computation)
	FcChar32	hash;
	XftFtFile* file; // face source
	// Rendering options
	FT_F26Dot6 xsize, ysize; // pixel size
	FcBool antialias;	// doing antialiasing
	FcBool embolden; // force emboldening
	FcBool color; // contains color glyphs
	int rgba; // subpixel order
	int lcd_filter; // lcd filter
	FT_Matrix matrix;	// glyph transformation matrix
	FcBool transform;	// non-identify matrix?
	FT_Int load_flags; // glyph load flags
	// Internal fields
	int spacing;
	FcBool minspace;
	int char_width;
};

/*
 * Internal version of the font with private data
 */
typedef struct XftFontInt {
	XftFont public;		/* public fields */
	XftFont* next;		/* all fonts on display */
	XftFont* hash_next; // fonts in this hash chain
	XftFontInfo info;	// Data from pattern
	int ref;	// reference count
	// Per-glyph information, indexed by glyph ID
	// This array follows the font in memory
	XftGlyph** glyphs;
	int num_glyphs; // size of glyphs/bitmaps arrays
	/*
	 * Hash table to get from Unicode value to glyph ID
	 * This array follows the glyphs in memory
	 */
	XftUcsHash		*hash_table;
	int			hash_value;
	int			rehash_value;
	// X specific fields
	GlyphSet glyphset; // Render glyphset
	XRenderPictFormat* format;	// Render format for glyphs
	// Glyph memory management fields
	unsigned long glyph_memory;
	unsigned long max_glyph_memory;
} XftFontInt;

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

#define XFT_NUM_FONT_HASH 127

typedef struct XftDisplayInfo {
	XExtCodes* codes;
	FcPattern* defaults;
	XftFont* fonts;
	unsigned long glyph_memory;
	unsigned long max_glyph_memory;
	int num_unref_fonts;
	int max_unref_fonts;
	XftSolidColor colors[XFT_NUM_SOLID_COLOR];
	XftFont* fontHash[XFT_NUM_FONT_HASH];
} XftDisplayInfo;

// By default, use no more than 4 meg of server memory total, and no
// more than 1 meg for any one font
#define XFT_DPY_MAX_GLYPH_MEMORY (4 * 1024 * 1024)
#define XFT_FONT_MAX_GLYPH_MEMORY (1024 * 1024)

// By default, keep the last 16 unreferenced fonts around to
// speed reopening them.  Note that the glyph caching code
// will keep the global memory usage reasonably limited
#define XFT_DPY_MAX_UNREF_FONTS 16

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
