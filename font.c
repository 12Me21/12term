#include <errno.h>
#include <math.h>
#include <X11/Xft/Xft.h>

#include "common.h"
#include "font.h"
#include "buffer.h"
#include "x.h"

#define Font Font_
typedef struct {
	Px width, height;
	Px ascent, descent;
	//Px lbearing, rbearing;
	
	bool badslant, badweight;
	XftFont* match;
	FcFontSet* set;
	FcPattern* pattern;
} Font;

static Font fonts[4]; // normal, bold, italic, bold+italic

// fonts loaded for fallback chars
// todo: unify these structures to simplify things
typedef struct {
	XftFont* font;
	int flags;
	Char unicodep;
} Fontcache;

static Fontcache* frc = NULL;
static int frclen = 0;
static int frccap = 0;

static int ceildiv(int a, int b) {
	return (a+b-1)/b;
}

static int load_font(Font* f, FcPattern* pattern) {
	FcPattern* configured = FcPatternDuplicate(pattern);
	if (!configured)
		return 1;
	
	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	XftDefaultSubstitute(W.d, W.scr, configured);
	
	FcResult result;
	FcPattern* match = FcFontMatch(NULL, configured, &result);
	if (!match) {
		FcPatternDestroy(configured);
		return 1;
	}
	
	f->match = XftFontOpenPattern(W.d, match);
	if (!f->match) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return 1;
	}
	
	int wantattr, haveattr;
	
	// check slant/weight to see if
	if (FcPatternGetInteger(pattern, "slant", 0, &wantattr) == FcResultMatch) {
		if (FcPatternGetInteger(f->match->pattern, "slant", 0, &haveattr)!=FcResultMatch || haveattr<wantattr) {
			f->badslant = true;
		}
	}
	if (FcPatternGetInteger(pattern, "weight", 0, &wantattr) == FcResultMatch) {
		if (FcPatternGetInteger(f->match->pattern, "weight", 0, &haveattr)!=FcResultMatch || haveattr != wantattr) {
			f->badweight = true;
		}
	}
	
	// calculate the average char width
	const char ascii_printable[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
	int len = strlen(ascii_printable);
	XGlyphInfo extents;
	XftTextExtentsUtf8(W.d, f->match, (const FcChar8*)ascii_printable, len, &extents);
	f->width = ceildiv(extents.xOff, len);
	
	f->set = NULL;
	f->pattern = configured;
	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	//f->lbearing = 0;
	//f->rbearing = f->match->max_advance_width;
	f->height = f->ascent + f->descent;
	
	return 0;
}

void init_fonts(const char* fontstr, double fontsize) {
	FcPattern* pattern;
		
	if (fontstr[0] == '-')
		pattern = XftXlfdParse(fontstr, False, False);
	else
		pattern = FcNameParse((const FcChar8*)fontstr);
	
	if (!pattern)
		die("can't open font %s\n", fontstr);
	
	if (fontsize) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontsize);
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontsize) == FcResultMatch) {
			//
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontsize) == FcResultMatch) {
			//
		} else {
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			fontsize = 12;
		}
	}
	
	time_log("parsed font pattern");
	
	load_font(&fonts[0], pattern);
	
	time_log("loaded font 0");
	
	// italic
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	load_font(&fonts[2], pattern);
	time_log("loaded font 2");
	
	// bold+italic
	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	load_font(&fonts[3], pattern);
	time_log("loaded font 3");
	// bold
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	load_font(&fonts[1], pattern);
	time_log("loaded font 1");
	
	W.font_ascent = fonts[0].ascent;
	
	// messy. remember to call update_charsize 
	W.cw = ceil(fonts[0].width);
	W.ch = ceil(fonts[0].height);
	
	FcPatternDestroy(pattern);
}

static void find_fallback_font(Char chr, int fontnum, XftFont** xfont, FT_UInt* glyph) {
	/* Fallback on font cache, search the font cache for match. */
	for (int f=0; f<frclen; f++) {
		*glyph = XftCharIndex(W.d, frc[f].font, chr);
		/* Everything correct. */
		if (*glyph && frc[f].flags==fontnum) {
			*xfont = frc[f].font;
			return;
		}
		/* We got a default font for a not found glyph. */
		if (!*glyph && frc[f].flags==fontnum && frc[f].unicodep==chr) {
			*xfont = frc[f].font;
			return;
		}
	}
	Font* font = &fonts[fontnum];
	/* Nothing was found. Use fontconfig to find matching font. */
	FcResult fcres;
	if (!font->set)
		font->set = FcFontSort(NULL, font->pattern, true, NULL, &fcres);
		
	FcPattern* fcpattern = FcPatternDuplicate(font->pattern);
	FcCharSet* fccharset = FcCharSetCreate();
		
	FcCharSetAddChar(fccharset, chr);
	FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
	FcPatternAddBool(fcpattern, FC_SCALABLE, true);
		
	FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
	FcDefaultSubstitute(fcpattern);
		
	FcPattern* fontpattern = FcFontSetMatch(NULL, (FcFontSet*[]){font->set}, 1, fcpattern, &fcres);
		
	/* Allocate memory for the new cache entry. */
	if (frclen >= frccap) {
		frccap += 16;
		REALLOC(frc, frccap);
	}
	
	frc[frclen].font = XftFontOpenPattern(W.d, fontpattern);
	if (!frc[frclen].font)
		die("XftFontOpenPattern failed seeking fallback font: %s\n", strerror(errno));
	frc[frclen].flags = fontnum;
	frc[frclen].unicodep = chr;
	
	*glyph = XftCharIndex(W.d, frc[frclen].font, chr);
	
	*xfont = frc[frclen].font;
	frclen++;
	
	FcPatternDestroy(fcpattern);
	FcCharSetDestroy(fccharset);
}

// ok so basically
// to transform a line into glyphs,
// first, we need to get the actual unicode chars
// skip dummy wide char halves, handle combining chars, etc.
// then once we have that, we can use harfbuzz to get the glyphs from this
// then, we need to do an additional pass to handle fallbacks with unknown chars

// todo: we should cache this for all the text onscreen mayb
int make_glyphs(int len, XftGlyphFontSpec specs[len], /*const*/ Cell cells[len], int indexs[len]) {
	int numspecs = 0;
	
	for (int i=0; i<len; i++) {
		// Fetch chr and mode for current glyph.
		Char chr = cells[i].chr;
		Attrs attrs = cells[i].attrs;
		
		/* Skip dummy wide-character spacing. */
		if (cells[i].wide == -1)
			continue;
		
		// empty cells
		if (chr == 0)
			continue;
		
		int fontnum = (attrs.weight==1) | (attrs.italic)<<1;
		
		Font* font = &fonts[fontnum];
		
		FT_UInt glyph = XftCharIndex(W.d, font->match, chr);
		XftFont* xfont;
		if (glyph)
			xfont = font->match;
		else
			find_fallback_font(chr, fontnum, &xfont, &glyph);
		
		specs[numspecs] = (XftGlyphFontSpec){
			.font = xfont,
			.glyph = glyph,
			.x = 0,
			.y = font->ascent,
		};
		indexs[numspecs] = i;
		numspecs++;
	}
	
	return numspecs;
}

void fonts_free(void) {
	for (int i=0; i<4; i++) {
		FcPatternDestroy(fonts[i].pattern);
		XftFontClose(W.d, fonts[i].match);
	}
	for (int i=0; i<frclen; i++)
		XftFontClose(W.d, frc[i].font);
	free(frc);
}
