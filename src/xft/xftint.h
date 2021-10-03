#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xmd.h>
#include <X11/Xlibint.h>
#define _XFT_NO_COMPAT_
#include "Xft.h"
#include <fontconfig/fcprivate.h>
#include <fontconfig/fcfreetype.h>

typedef struct _XftMatcher {
	char* object;
	double (*compare)(char* object, FcValue value1, FcValue value2);
} XftMatcher;

typedef struct _XftSymbolic {
	const char* name;
	int value;
} XftSymbolic;

/*
 * Glyphs are stored in this structure
 */
typedef struct _XftGlyph {
	XGlyphInfo metrics;
	void* bitmap;
	unsigned long glyph_memory;
	Picture picture;
} XftGlyph;

/*
 * A hash table translates Unicode values into glyph indicies
 */
typedef struct _XftUcsHash {
	FcChar32 ucs4;
	FT_UInt glyph;
} XftUcsHash;

/*
 * Many fonts can share the same underlying face data; this
 * structure references that.  Note that many faces may in fact
 * live in the same font file; that is irrelevant to this structure
 * which is concerned only with the individual faces themselves
 */
typedef struct _XftFtFile {
	struct _XftFtFile	*next;
	int ref; // number of font infos using this file
	
	char* file;	// file name
	int id; // font index within that file
	
	FT_F26Dot6 xsize;	// current xsize setting
	FT_F26Dot6 ysize;	// current ysize setting
	FT_Matrix matrix;	// current matrix setting
	
	int lock; // lock count; can't unload unless 0 */
	FT_Face face; // pointer to face; only valid when lock
} XftFtFile;

/*
 * This structure holds the data extracted from a pattern
 * needed to create a unique font object.
 */
struct _XftFontInfo {
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
	FcBool render; // whether to use the Render extension
	// Internal fields
	int spacing;
	FcBool minspace;
	int char_width;
};

/*
 * Internal version of the font with private data
 */
typedef struct _XftFontInt {
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
	bool use_free_glyphs; // Use XRenderFreeGlyphs
} XftFontInt;

typedef enum _XftClipType {
	XftClipTypeNone, XftClipTypeRegion, XftClipTypeRectangles
} XftClipType;

typedef struct _XftClipRect {
	int xOrigin;
	int yOrigin;
	int n;
} XftClipRect;

#define XftClipRects(cr) ((XRectangle*) ((cr)+1))

typedef union _XftClip {
	XftClipRect* rect;
	Region region;
} XftClip;

struct _XftDraw {
	Display* dpy;
	int screen;
	unsigned int bits_per_pixel;
	unsigned int depth;
	Drawable drawable;
	Visual* visual;	/* NULL for bitmaps */
	Colormap colormap;
	XftClipType clip_type;
	XftClip clip;
	int subwindow_mode;
	struct {
		Picture pict;
	} render;
};

/*
 * Instead of taking two round trips for each blending request,
 * assume that if a particular drawable fails GetImage that it will
 * fail for a "while"; use temporary pixmaps to avoid the errors
 */

#define XFT_ASSUME_PIXMAP 20

typedef struct _XftSolidColor {
	XRenderColor color;
	int screen;
	Picture pict;
} XftSolidColor;

#define XFT_NUM_SOLID_COLOR 16

#define XFT_NUM_FONT_HASH 127

typedef struct _XftDisplayInfo {
	struct _XftDisplayInfo* next;
	Display* display;
	XExtCodes* codes;
	FcPattern* defaults;
	bool hasSolid;
	XftFont* fonts;
	XRenderPictFormat* solidFormat;
	unsigned long glyph_memory;
	unsigned long max_glyph_memory;
	bool use_free_glyphs;
	int num_unref_fonts;
	int max_unref_fonts;
	XftSolidColor colors[XFT_NUM_SOLID_COLOR];
	XftFont* fontHash[XFT_NUM_FONT_HASH];
} XftDisplayInfo;

/*
 * By default, use no more than 4 meg of server memory total, and no
 * more than 1 meg for any one font
 */
#define XFT_DPY_MAX_GLYPH_MEMORY (4 * 1024 * 1024)
#define XFT_FONT_MAX_GLYPH_MEMORY (1024 * 1024)

/*
 * By default, keep the last 16 unreferenced fonts around to
 * speed reopening them.  Note that the glyph caching code
 * will keep the global memory usage reasonably limited
 */
#define XFT_DPY_MAX_UNREF_FONTS 16

extern XftDisplayInfo* _XftDisplayInfo;

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

/* xftcompat.c */
void XftFontSetDestroy(FcFontSet* s);
bool XftMatrixEqual(const FcMatrix* mat1, const FcMatrix* mat2);
void XftMatrixMultiply(FcMatrix* result, FcMatrix* a, FcMatrix* b);
void XftMatrixRotate(FcMatrix* m, double c, double s);
void XftMatrixScale(FcMatrix* m, double sx, double sy);
void XftMatrixShear(FcMatrix* m, double sh, double sv);
FcPattern* XftPatternCreate(void);
void XftValueDestroy(FcValue v);
void XftPatternDestroy(FcPattern* p);
bool XftPatternAdd(FcPattern* p, const char* object, FcValue value, bool append);
bool XftPatternDel(FcPattern* p, const char* object);
bool XftPatternAddInteger(FcPattern* p, const char* object, int i);
bool XftPatternAddDouble(FcPattern* p, const char* object, double i);
bool XftPatternAddMatrix(FcPattern* p, const char* object, FcMatrix* i);
bool XftPatternAddString(FcPattern* p, const char* object, char* i);
bool XftPatternAddBool(FcPattern* p, const char* object, bool i);
FcResult XftPatternGet(FcPattern* p, const char* object, int id, FcValue* v);
FcResult XftPatternGetInteger(FcPattern* p, const char* object, int id, int* i);
FcResult XftPatternGetDouble(FcPattern* p, const char* object, int id, double* i);
FcResult XftPatternGetString(FcPattern* p, const char* object, int id, char** i);
FcResult XftPatternGetMatrix(FcPattern* p, const char* object, int id, FcMatrix** i);
FcResult XftPatternGetBool(FcPattern* p, const char* object, int id, bool* i);
FcPattern* XftPatternDuplicate(FcPattern* orig);
FcPattern* XftPatternVaBuild(FcPattern* orig, va_list va);
FcPattern* XftPatternBuild(FcPattern* orig, ...);
bool XftNameUnparse(FcPattern* pat, char* dest, int len);
bool XftGlyphExists(Display* dpy, XftFont* font, FcChar32 ucs4);
FcObjectSet* XftObjectSetCreate(void);
Bool XftObjectSetAdd(FcObjectSet* os, const char* object);
void XftObjectSetDestroy(FcObjectSet* os);
FcObjectSet* XftObjectSetVaBuild(const char* first, va_list va);
FcObjectSet* XftObjectSetBuild(const char* first, ...);
FcFontSet* XftListFontSets(FcFontSet** sets, int nsets, FcPattern* p, FcObjectSet* os);

/* xftdbg.c */
int XftDebug(void);

/* xftdpy.c */
XftDisplayInfo* _XftDisplayInfoGet(Display* dpy, bool createIfNecessary);
void _XftDisplayManageMemory(Display* dpy);
int XftDefaultParseBool(const char* v);
bool XftDefaultGetBool(Display* dpy, const char* object, int screen, bool def);
int XftDefaultGetInteger(Display* dpy, const char* object, int screen, int def);
double XftDefaultGetDouble(Display* dpy, const char* object, int screen, double def);
FcFontSet* XftDisplayGetFontSet(Display* dpy);

/* xftdraw.c */
unsigned int XftDrawDepth(XftDraw* draw);
unsigned int XftDrawBitsPerPixel(XftDraw* draw);
bool XftDrawRenderPrepare(XftDraw* draw);

/* xftfreetype.c */
bool _XftSetFace(XftFtFile* f, FT_F26Dot6 xsize, FT_F26Dot6 ysize, FT_Matrix* matrix);
void XftFontManageMemory(Display* dpy);

/* xftglyph.c */
void _XftFontUncacheGlyph(Display* dpy, XftFont* public);
void _XftFontManageMemory(Display* dpy, XftFont* public);

/* xftinit.c */
void XftMemReport(void);
void XftMemAlloc(int kind, int size);
void* XftMalloc(int kind, size_t size);
void XftMemFree(int kind, int size);

/* xftlist.c */
FcFontSet* XftListFontsPatternObjects(Display* dpy, int screen, FcPattern* pattern, FcObjectSet* os);

/* xftname.c */
void _XftNameInit(void);

/* xftswap.c */
int XftNativeByteOrder(void);
void XftSwapCARD32(CARD32* data, int n);
void XftSwapCARD24(CARD8* data, int width, int height);
void XftSwapCARD16(CARD16* data, int n);
void XftSwapImage(XImage* image);
