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
	
	struct XftFontInfo info; // Data from pattern (i feel like this could be in one struct?)
	// X specific fields
	char format;
	
	struct XftFont* next; // linked list
} XftFont;

typedef struct XftFormat {
	XRenderPictFormat* format;
	GlyphSet glyphset;
	int next_glyph;
} XftFormat;

extern XftFormat xft_formats[PictStandardNUM];

#define XFT_NUM_SOLID_COLOR 16

/* xftfreetype.c */
FT_Face xft_lock_face(XftFont* pub);
void XftFontClose(XftFont* pub);
XftFont* XftFontOpenPattern(FcPattern* pattern);

/* xftglyph.c */
bool load_glyph(XftFont* font, Char chr, GlyphData* out);

// bitmap.c

int compute_xrender_bitmap_size(FT_Bitmap* target, const FT_Bitmap* ftbit, FT_Render_Mode mode, const FT_Matrix* matrix);

void fill_xrender_bitmap(FT_Bitmap* target, const FT_Bitmap* ftbit, FT_Render_Mode mode, bool bgr, const FT_Matrix* matrix);

//void fill_xrender_bitmap(FT_Bitmap* target, const FT_Bitmap* ftbit, FT_Render_Mode mode, bool bgr, const FT_Matrix* matrix);
