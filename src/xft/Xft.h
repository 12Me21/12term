#pragma once

#include <stdarg.h>
#include <stdbool.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <fontconfig/fontconfig.h>

#include <X11/extensions/Xrender.h>

#include "../common.h"

#define XFT_MAX_GLYPH_MEMORY	"maxglyphmemory"
#define XFT_MAX_UNREF_FONTS	"maxunreffonts"

extern FT_Library	ft_library;

// A hash table translates Unicode values into glyph indicies
typedef struct XftUcsHash {
	FcChar32 ucs4;
	FT_UInt glyph;
} XftUcsHash;

// Glyphs are stored in this structure
typedef struct XftGlyph {
	XGlyphInfo metrics;
	unsigned long glyph_memory;
	Picture picture;
} XftGlyph;

// Many fonts can share the same underlying face data; this
// structure references that.  Note that many faces may in fact
// live in the same font file; that is irrelevant to this structure
// which is concerned only with the individual faces themselves
typedef struct XftFtFile {
	struct XftFtFile* next;
	int ref; // number of font infos using this file
	
	char* file;	// file name
	int id; // font index within that file
	
	FT_F26Dot6 xsize;	// current xsize setting
	FT_F26Dot6 ysize;	// current ysize setting
	FT_Matrix matrix;	// current matrix setting
	
	int lock; // lock count; can't unload unless 0
	FT_Face face; // pointer to face; only valid when lock
} XftFtFile;

// This structure holds the data extracted from a pattern
// needed to create a unique font object.
typedef struct XftFontInfo {
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
} XftFontInfo;

typedef struct XftFont {
	int ascent, descent, height;
	int max_advance_width;
	FcCharSet* charset;
	FcPattern* pattern;
	
	struct XftFont* next; // all fonts on display
	XftFontInfo info;	// Data from pattern
	int ref;	// reference count
	// Per-glyph information, indexed by glyph ID
	// This array follows the font in memory
	XftGlyph** glyphs;
	int num_glyphs; // size of glyphs/bitmaps arrays
	// Hash table to get from Unicode value to glyph ID
	// This array follows the glyphs in memory
	XftUcsHash* hash_table;
	int hash_value;
	int rehash_value;
	// X specific fields
	GlyphSet glyphset; // Render glyphset
	XRenderPictFormat* format;	// Render format for glyphs
	// Glyph memory management fields
	unsigned long glyph_memory;
	unsigned long max_glyph_memory;
} XftFont;

typedef struct XftDraw XftDraw;

typedef struct XftGlyphFontSpec {
	XftFont* font;
	FT_UInt glyph;
	short	x, y;
} XftGlyphFontSpec;

// ghhh
typedef struct XftGlyphFont {
	XftFont* font;
	FT_UInt glyph;
} XftGlyphFont;

/* xftcolor.c */
unsigned long XftColorAllocValue(const XRenderColor* color);

/* xftdpy.c */
bool XftDefaultSet(FcPattern* defaults);
void XftDefaultSubstitute(FcPattern* pattern);
void XftDisplayInfoInit(void);

/* xftdraw.c */
XftDraw* XftDrawCreate(Px w, Px h);

Drawable XftDrawDrawable(XftDraw* draw);

void XftDrawDestroy(XftDraw* draw);

Picture XftDrawPicture(XftDraw* draw);

Picture XftDrawSrcPicture(const XRenderColor color);

void XftDrawRect(XftDraw* draw, const XRenderColor color, Px x, Px y, Px width, Px height);

void XftDrawPut(XftDraw* draw, Px x, Px y, Px w, Px h, Px dx, Px dy);

/* xftextent.c */

void XftGlyphExtents(XftFont* pub, const FT_UInt* glyphs, int nglyphs, XGlyphInfo* extents);
void XftTextExtents32(XftFont* pub, const FcChar32* string, int len, XGlyphInfo* extents);

/* xftfreetype.c */

FT_Face XftLockFace(XftFont* pub);
void XftUnlockFace(XftFont* pub);
XftFontInfo* XftFontInfoCreate(const FcPattern* pattern);
void XftFontInfoDestroy(XftFontInfo* fi);
FcChar32 XftFontInfoHash(const XftFontInfo* fi);
bool XftFontInfoEqual(const XftFontInfo* a, const XftFontInfo* b);
XftFont* XftFontOpenInfo(FcPattern* pattern, XftFontInfo* fi);
XftFont* XftFontOpenPattern(FcPattern* pattern);
void XftFontClose(XftFont* pub);

/* xftglyphs.c */
void XftFontLoadGlyphs(XftFont* pub, bool need_bitmaps, const FT_UInt* glyphs, int nglyph);
void XftFontUnloadGlyphs(XftFont* pub, const FT_UInt* glyphs, int nglyph);

#define XFT_NMISSING 256

bool XftFontCheckGlyph(XftFont* pub, bool need_bitmaps, FT_UInt glyph, FT_UInt* missing, int* nmissing);
bool XftCharExists(XftFont* pub, FcChar32 ucs4);
FT_UInt XftCharIndex(XftFont* pub, FcChar32 ucs4);

/* xftrender.c */

// eee
void XftGlyphRender1(int op, XRenderColor col, XftFont* pub, Picture dst, int x, int y, FT_UInt g, int cw);