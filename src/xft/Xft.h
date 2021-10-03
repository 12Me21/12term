#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <X11/extensions/Xrender.h>

#define XFT_CORE		"core"
#define XFT_RENDER		"render"
#define XFT_XLFD		"xlfd"
#define XFT_MAX_GLYPH_MEMORY	"maxglyphmemory"
#define XFT_MAX_UNREF_FONTS	"maxunreffonts"

extern FT_Library	_XftFTlibrary;

typedef struct _XftFontInfo XftFontInfo;

typedef struct _XftFont {
	int ascent, descent, height;
	int max_advance_width;
	FcCharSet* charset;
	FcPattern* pattern;
} XftFont;

typedef struct _XftDraw XftDraw;

typedef struct _XftColor {
	unsigned long pixel;
	XRenderColor color;
} XftColor;

typedef struct _XftCharSpec {
	FcChar32	ucs4;
	short	x, y;
} XftCharSpec;

typedef struct _XftCharFontSpec {
	XftFont* font;
	FcChar32	ucs4;
	short	x, y;
} XftCharFontSpec;

typedef struct _XftGlyphSpec {
	FT_UInt glyph;
	short	x, y;
} XftGlyphSpec;

typedef struct _XftGlyphFontSpec {
	XftFont* font;
	FT_UInt glyph;
	short	x, y;
} XftGlyphFontSpec;

// ghhh
typedef struct _XftGlyphFont {
	XftFont* font;
	FT_UInt glyph;
} XftGlyphFont;

/* xftcolor.c */
Bool XftColorAllocName(Display* dpy, const Visual* visual, Colormap cmap, const char* name, XftColor* result);
Bool XftColorAllocValue(Display* dpy, Visual* visual, Colormap cmap, const XRenderColor* color, XftColor* result);

/* xftdpy.c */
Bool XftDefaultHasRender(Display* dpy);

Bool XftDefaultSet(Display* dpy, FcPattern* defaults);

void XftDefaultSubstitute(Display* dpy, int screen, FcPattern* pattern);

/* xftdraw.c */

XftDraw* 
XftDrawCreate(Display* dpy, Drawable drawable,
 Visual* visual,
 Colormap colormap);

XftDraw* XftDrawCreateBitmap(Display* dpy, Pixmap bitmap);

XftDraw* XftDrawCreateAlpha(Display* dpy, Pixmap pixmap, int depth);

void XftDrawChange(XftDraw* draw, Drawable drawable);

Display* XftDrawDisplay(XftDraw* draw);

Drawable XftDrawDrawable(XftDraw* draw);

Colormap XftDrawColormap(XftDraw* draw);

Visual* XftDrawVisual(XftDraw* draw);

void XftDrawDestroy(XftDraw* draw);

Picture XftDrawPicture(XftDraw* draw);

Picture XftDrawSrcPicture(XftDraw* draw, const XftColor* color);

void XftDrawGlyphs(XftDraw* draw, const XftColor* color, XftFont* pub, int x, int y, const FT_UInt* glyphs, int nglyphs);

void
XftDrawString8(XftDraw* draw,
 const XftColor* color,
 XftFont* pub,
 int x,
 int y,
 const FcChar8* string,
 int len);

void
XftDrawString16(XftDraw* draw,
 const XftColor* color,
 XftFont* pub,
 int x,
 int y,
 const FcChar16* string,
 int len);

void
XftDrawString32(XftDraw* draw,
 const XftColor* color,
 XftFont* pub,
 int x,
 int y,
 const FcChar32* string,
 int len);

void
XftDrawCharSpec(XftDraw* draw,
 const XftColor* color,
 XftFont* pub,
 const XftCharSpec* chars,
 int len);

void
XftDrawCharFontSpec(XftDraw* draw,
 const XftColor* color,
 const XftCharFontSpec* chars,
 int len);

void
XftDrawGlyphSpec(XftDraw* draw,
 const XftColor* color,
 XftFont* pub,
 const XftGlyphSpec* glyphs,
 int len);

void
XftDrawGlyphFontSpec(XftDraw* draw,
 const XftColor* color,
 const XftGlyphFontSpec* glyphs,
 int len);

void
XftDrawRect(XftDraw* draw,
 const XftColor* color,
 int x,
 int y,
 unsigned int width,
 unsigned int height);


Bool
XftDrawSetClip(XftDraw* draw,
 Region r);


Bool
XftDrawSetClipRectangles(XftDraw* draw,
 int xOrigin,
 int yOrigin,
 const XRectangle* rects,
 int n);

void
XftDrawSetSubwindowMode(XftDraw* draw,
 int mode);

/* xftextent.c */

void
XftGlyphExtents(Display* dpy,
 XftFont* pub,
 const FT_UInt* glyphs,
 int nglyphs,
 XGlyphInfo* extents);

void
XftTextExtents32(Display* dpy,
 XftFont* pub,
 const FcChar32* string,
 int len,
 XGlyphInfo* extents);

/* xftfont.c */
FcPattern* 
XftFontMatch(Display* dpy,
 int screen,
 const FcPattern* pattern,
 FcResult* result);

XftFont* 
XftFontOpen(Display* dpy, int screen, ...) _X_SENTINEL(0);

XftFont* 
XftFontOpenName(Display* dpy, int screen, const char* name);

XftFont* 
XftFontOpenXlfd(Display* dpy, int screen, const char* xlfd);

/* xftfreetype.c */

FT_Face XftLockFace(XftFont* pub);

void XftUnlockFace(XftFont* pub);

XftFontInfo* XftFontInfoCreate(Display* dpy, const FcPattern* pattern);

void XftFontInfoDestroy(Display* dpy, XftFontInfo* fi);

FcChar32 XftFontInfoHash(const XftFontInfo* fi);

bool XftFontInfoEqual(const XftFontInfo* a, const XftFontInfo* b);

XftFont* XftFontOpenInfo(Display* dpy, FcPattern* pattern, XftFontInfo* fi);

XftFont* XftFontOpenPattern(Display* dpy, FcPattern* pattern);

XftFont* XftFontCopy(Display* dpy, XftFont* pub);

void XftFontClose(Display* dpy, XftFont* pub);

bool XftInitFtLibrary(void);

/* xftglyphs.c */
void
XftFontLoadGlyphs(Display * dpy,
 XftFont* pub,
 bool need_bitmaps,
 const FT_UInt* glyphs,
 int nglyph);

void
XftFontUnloadGlyphs(Display* dpy,
 XftFont* pub,
 const FT_UInt* glyphs,
 int nglyph);

#define XFT_NMISSING 256

bool
XftFontCheckGlyph(Display* dpy,
 XftFont* pub,
 bool need_bitmaps,
 FT_UInt glyph,
 FT_UInt* missing,
 int* nmissing);

bool
XftCharExists(Display* dpy,
 XftFont* pub,
 FcChar32 ucs4);

FT_UInt
XftCharIndex(Display* dpy,
 XftFont* pub,
 FcChar32 ucs4);

/* xftlist.c */

FcFontSet* XftListFonts(Display* dpy,
 int screen,
 ...) _X_SENTINEL(0);

/* xftname.c */
FcPattern
*XftNameParse(const char* name);

/* xftrender.c */
void
XftGlyphRender(Display* dpy,
 int op,
 Picture src,
 XftFont* pub,
 Picture dst,
 int srcx,
 int srcy,
 int x,
 int y,
 const FT_UInt* glyphs,
 int nglyphs);

void
XftGlyphSpecRender(Display* dpy,
 int op,
 Picture src,
 XftFont* pub,
 Picture dst,
 int srcx,
 int srcy,
 const XftGlyphSpec* glyphs,
 int nglyphs);

void
XftCharSpecRender(Display* dpy,
 int op,
 Picture src,
 XftFont* pub,
 Picture dst,
 int srcx,
 int srcy,
 const XftCharSpec* chars,
 int len);

void
XftGlyphFontSpecRender(Display* dpy,
 int op,
 Picture src,
 Picture dst,
 int srcx,
 int srcy,
 const XftGlyphFontSpec* glyphs,
 int nglyphs);

void
XftCharFontSpecRender(Display* dpy,
 int op,
 Picture src,
 Picture dst,
 int srcx,
 int srcy,
 const XftCharFontSpec* chars,
 int len);

void
XftTextRender8(Display* dpy,
 int op,
 Picture src,
 XftFont* pub,
 Picture dst,
 int srcx,
 int srcy,
 int x,
 int y,
 const FcChar8* string,
 int len);

void
XftTextRender16(Display* dpy,
 int op,
 Picture src,
 XftFont* pub,
 Picture dst,
 int srcx,
 int srcy,
 int x,
 int y,
 const FcChar16* string,
 int len);

void
XftTextRender16BE(Display* dpy,
 int op,
 Picture src,
 XftFont* pub,
 Picture dst,
 int srcx,
 int srcy,
 int x,
 int y,
 const FcChar8* string,
 int len);

void
XftTextRender16LE(Display* dpy,
 int op,
 Picture src,
 XftFont* pub,
 Picture dst,
 int srcx,
 int srcy,
 int x,
 int y,
 const FcChar8* string,
 int len);

void
XftTextRender32(Display* dpy,
 int op,
 Picture src,
 XftFont* pub,
 Picture dst,
 int srcx,
 int srcy,
 int x,
 int y,
 const FcChar32* string,
 int len);

void XftTextRender32BE(Display* dpy, int op, Picture src, XftFont* pub, Picture dst, int srcx, int srcy, int x, int y, const FcChar8* string, int len);

void XftTextRender32LE(Display* dpy, int op, Picture src, XftFont* pub, Picture dst, int srcx, int srcy, int x, int y, const FcChar8* string, int len);

/* xftxlfd.c */
FcPattern* XftXlfdParse(const char* xlfd_orig, Bool ignore_scalable, Bool complete);

// eee
void XftGlyphRender1(Display* dpy, int	op, Picture src, XftFont* pub, Picture dst, int srcx, int srcy, int x, int y, const FT_UInt g, int cw);
