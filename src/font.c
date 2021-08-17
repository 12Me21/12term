// Loading fonts and looking up glyphs

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
	
	XftFont* font;
	FcFontSet* set;
	FcPattern* pattern;
} Font;

static Font fonts[4] = {0}; // normal, bold, italic, bold+italic

static int ceildiv(int a, int b) {
	return (a+b-1)/b;
}

//load one font face
static bool load_font(Font* f, FcPattern* pattern, bool bold, bool italic) {
	FcPattern* configured = FcPatternDuplicate(pattern);
	if (!configured)
		return false;
	
	if (italic)
		FcPatternAddInteger(configured, FC_SLANT, FC_SLANT_ITALIC);
	if (bold)
		FcPatternAddInteger(configured, FC_WEIGHT, FC_WEIGHT_BOLD);
	
	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	XftDefaultSubstitute(W.d, W.scr, configured);
	
	FcResult result;
	FcPattern* match = FcFontMatch(NULL, configured, &result);
	if (!match) {
		FcPatternDestroy(configured);
		return false;
	}
	
	f->font = XftFontOpenPattern(W.d, match);
	if (!f->font) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return false;
	}
	
	// calculate the average char width
	const utf8 ascii_printable[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
	int len = strlen(ascii_printable);
	XGlyphInfo extents;
	XftTextExtentsUtf8(W.d, f->font, (const FcChar8*)ascii_printable, len, &extents);
	f->width = ceildiv(extents.xOff, len);
	
	f->set = NULL;
	f->pattern = configured;
	f->ascent = f->font->ascent;
	f->descent = f->font->descent;
	f->height = f->ascent + f->descent;
	
	return true;
}

// This frees any existing fonts and loads new ones, based on `fontstr`.
// it also sets `W.cw` and `W.ch`.
void load_fonts(const utf8* fontstr, double fontsize) {
	fonts_free();
	
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
		FcPatternAddDouble(pattern, FC_SIZE, fontsize);
	}
	
	for (int i=0; i<4; i++) {
		if (!load_font(&fonts[i], pattern, i&1, i&2))
			die("failed to load font");
		time_log("loaded font");
	}
	
	W.font_ascent = fonts[0].ascent;
	
	// messy. remember to call update_charsize 
	W.cw = ceil(fonts[0].width);
	W.ch = ceil(fonts[0].height);
	
	FcPatternDestroy(pattern);
}

// fonts loaded for fallback chars
typedef struct {
	XftFont* font;
	int style;
	Char unicodep;
} Fontcache;

static Fontcache frc[100];
static int frclen = 0;

// this is gross and I don't fully understand how it works
static void find_fallback_font(Char chr, int style, XftFont** xfont, FT_UInt* glyph) {
	// Fallback on font cache, search the font cache for match.
	for (int f=0; f<frclen; f++) {
		*glyph = XftCharIndex(W.d, frc[f].font, chr);
		
		if (*glyph && frc[f].style==style) {
			*xfont = frc[f].font;
			return;
		}
		// We got a default font for a not found glyph.
		if (!*glyph && frc[f].style==style && frc[f].unicodep==chr) {
			*xfont = frc[f].font;
			return;
		}
	}
	Font* font = &fonts[style];
	// Nothing was found. Use fontconfig to find matching font.
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
	
	if (frclen >= LEN(frc))
		die("too many fallback fonts aaaaaaa\n");
	
	frc[frclen].font = XftFontOpenPattern(W.d, fontpattern);
	if (!frc[frclen].font)
		die("XftFontOpenPattern failed seeking fallback font: %s\n", strerror(errno));
	frc[frclen].style = style;
	frc[frclen].unicodep = chr;
	
	*glyph = XftCharIndex(W.d, frc[frclen].font, chr);
	
	*xfont = frc[frclen].font;
	frclen++;
	
	FcPatternDestroy(fcpattern);
	FcCharSetDestroy(fccharset);
}

static int cell_fontstyle(const Cell* c) {
	return (c->attrs.weight==1) | (c->attrs.italic)<<1;
}

// todo: maybe cache the results for all ascii chars or something

// this converts a row of cells into a row of glyphs+fonts
// todo: the output is kinda messy. maybe just output into our own struct of .x, .glyph, .font instead (where .x is a cell coordinate)
int make_glyphs(int len, XftGlyphFontSpec specs[len], /*const*/ Cell cells[len], int indexs[len], DrawnCell old[len]) {
	int numspecs = 0;
	
	for (int i=0; i<len; i++) {
		Char chr = cells[i].chr;
		
		// skip blank cells
		if (cells[i].wide==-1 || chr==0 || chr==' ') {
			if (old) {
				old[i].chr = chr;
				old[i].fontnum = -1;
				old[i].font = NULL;
				old[i].glyph = 0;
			}
			continue;
		}
		
		int style = cell_fontstyle(&cells[i]);
		
		XftFont* xfont;
		FT_UInt glyph;
		
		if (old && chr==old[i].chr && style==old[i].fontnum) {
			xfont = old[i].font;
			glyph = old[i].glyph;
		} else {
			Font* font = &fonts[style];
			glyph = XftCharIndex(W.d, font->font, chr);
			if (glyph)
				xfont = font->font;
			else
				find_fallback_font(chr, style, &xfont, &glyph);
			if (old) {
				old[i].font = xfont;
				old[i].glyph = glyph;
				old[i].chr = chr;
				old[i].fontnum = style;
			}
		}
		
		specs[numspecs] = (XftGlyphFontSpec){
			.font = xfont,
			.glyph = glyph,
			.x = 0,
			.y = W.font_ascent,//font->ascent,
		};
		indexs[numspecs] = i;
		numspecs++;
	}
	
	return numspecs;
}

void fonts_free(void) {
	for (int i=0; i<4; i++) {
		if (fonts[i].font) {
			FcPatternDestroy(fonts[i].pattern);
			XftFontClose(W.d, fonts[i].font);
			fonts[i].font = NULL;
		}
	}
	for (int i=0; i<frclen; i++)
		XftFontClose(W.d, frc[i].font);
}
