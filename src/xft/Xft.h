#pragma once

#include <stdarg.h>
#include <stdbool.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <fontconfig/fontconfig.h>

#include <X11/extensions/Xrender.h>

#include "../common.h"

extern FT_Library	ft_library;

// This structure holds the data extracted from a pattern
// needed to create a unique font object.
typedef struct XftFontInfo {
	struct FontFile* file; // face source
	// Rendering options
	FT_F26Dot6 xsize, ysize; // pixel size
	
	// these need to be FcBool because some functions output using pointers to these
	FcBool antialias;	// doing antialiasing
	FcBool embolden; // force emboldening
	FcBool color; // contains color glyphs
	int rgba; // subpixel order
	int lcd_filter; // lcd filter
	FT_Matrix matrix;	// glyph transformation matrix
	FcBool transform;	// non-identify matrix?
	FT_Int load_flags; // glyph load flags (passed to FT_Load_Glyph)
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
	struct XftFontInfo info; // Data from pattern (i feel like this could be in one struct?)
	int ref;	// reference count
	// X specific fields
	XRenderPictFormat* format; // Render format for glyphs
} XftFont;

// ghhh
typedef struct XftGlyphFont {
	XftFont* font;
	FT_UInt glyph;
} XftGlyphFont;

/* xftdpy.c */
bool XftDefaultSet(FcPattern* defaults);
void XftDefaultSubstitute(FcPattern* pattern);
void xft_init(void);

/* xftextent.c */

void xft_text_extents(XftFont* pub, const Char* string, int len, XGlyphInfo* extents);

/* xftfreetype.c */
XftFont* XftFontOpenPattern(FcPattern* pattern);
void XftFontClose(XftFont* pub);

/* xftglyphs.c */
void XftFontUnloadGlyphs(XftFont* pub, const FT_UInt* glyphs, int nglyph);

bool XftFontCheckGlyph(XftFont* pub, FT_UInt glyph, FT_UInt* missing, int* nmissing);
bool XftCharExists(XftFont* pub, Char ucs4);
FT_UInt XftCharIndex(XftFont* pub, Char ucs4);

/* xftrender.c */

// eee
void XftGlyphRender1(int op, XRenderColor col, XftFont* font, Picture dst, float x, int y, FT_UInt g);
Picture XftDrawSrcPicture(const XRenderColor color);


// this contains all the info needed to render a glyph
typedef struct GlyphData {
	XGlyphInfo metrics;
	XRenderPictFormat* format;
	// todo: there are only a few formats that we actually ever use
	// we call XRenderFindStandardFormat with one of:
	// PictStandardARGB32 - for color glyphs
	// PictStandardA8 - for normal fonts/glyphs
	// PictStandardA1 - for bitmap fonts
	// so instead of a pointer we could just store one of these types in a char
	
	union {
		Glyph id; // index in glyphset
		Picture picture; // for color glyphs
	};
	char type; // 0 = doesn't exist, 1 = glyph, 2 = picture
} GlyphData;

GlyphData* cache_lookup(Char chr, uint8_t style);

bool load_font(FcPattern* pattern, int style, bool bold, bool italic);

void render_glyph(int op, XRenderColor col, Picture dst, float x, int y, GlyphData* glyph);
