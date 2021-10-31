// Loading fonts and looking up glyphs

#include <errno.h>
#include <math.h>
#include "xft/Xft.h"

#include "common.h"
#include "font.h"
#include "buffer.h"
#include "settings.h"
#include "x.h"

#define Font Font_
typedef struct {
	Px width, height;
	
	XftFont* font;
	FcFontSet* set; // set of matching fonts, used for loading fallback chars
	FcPattern* pattern;
} Font;

static Font fonts[4] = {0}; // normal, bold, italic, bold+italic

void font_init(void) {
	if (!FcInit())
		die("fontconfig init failed");
	xft_init();
	if (FT_Init_FreeType(&ft_library))
		die("freetype init failed");
}

static int ceildiv(int a, int b) {
	return (a+b-1)/b;
}

//load one font face
static bool load_font(Font* f, FcPattern* pattern, bool bold, bool italic, bool get_width) {
	FcPattern* configured = FcPatternDuplicate(pattern);
	if (!configured)
		return false;
	
	if (italic)
		FcPatternAddInteger(configured, FC_SLANT, FC_SLANT_ITALIC);
	if (bold)
		FcPatternAddInteger(configured, FC_WEIGHT, FC_WEIGHT_BOLD);
	
	// todo: see how the config works, maybe we can use this to load defaults better
	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	pattern_default_substitute(configured);
	
	FcResult result;
	FcPattern* match = FcFontMatch(NULL, configured, &result);
	if (!match) {
		FcPatternDestroy(configured);
		return false;
	}
	
	f->font = XftFontOpenPattern(match);
	if (!f->font) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return false;
	}
	
	// calculate the average char width
	// (this also serves to load all ascii glyphs immediately)
	//if (get_width) {
		int len = 95;
		Char ascii_printable[len];
		for (int i=0; i<len; i++)
			ascii_printable[i] = ' '+i;
		XGlyphInfo extents;
		xft_text_extents(f->font, ascii_printable, len, &extents);
		f->width = ceildiv(extents.xOff, len);
		//}
	
	FcResult fcres;
	f->pattern = configured;
	f->set = FcFontSort(NULL, f->pattern, true, NULL, &fcres);
	//f->ascent = f->font->ascent;
	//f->descent = f->font->descent;
	f->height = f->font->ascent + f->font->descent;
	
	return true;
}

// This frees any existing fonts and loads new ones, based on `fontstr`.
// it also sets `W.cw` and `W.ch`.
void load_fonts(const utf8* fontstr, double fontsize) {
	print("loading pattern: %s\n", fontstr);
	
	fonts_free();
	
	FcPattern* pattern = FcNameParse((const FcChar8*)fontstr);
	
	if (!pattern)
		die("can't open font %s\n", fontstr);
	
	if (fontsize) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_SIZE, fontsize);
	}
	
	for (int i=0; i<4; i++) {
		if (!load_font(&fonts[i], pattern, i&1, i&2, i==0))
			die("failed to load font");
		time_log("loaded font");
	}
	
	W.font_baseline = fonts[0].font->ascent;
	
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
	//print("finding fallback font for %d\n", chr);
	// Fallback on font cache, search the font cache for match.
	for (int f=0; f<frclen; f++) {
		*glyph = XftCharIndex(frc[f].font, chr);
		
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
	
	FcPattern* fcpattern = FcPatternDuplicate(font->pattern);
	FcCharSet* fccharset = FcCharSetCreate();
		
	FcCharSetAddChar(fccharset, chr);
	FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
	FcPatternAddBool(fcpattern, FC_SCALABLE, true);
		
	FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
	FcDefaultSubstitute(fcpattern);
	
	FcResult fcres;
	FcPattern* fontpattern = FcFontSetMatch(NULL, &font->set, 1, fcpattern, &fcres);
	
	if (frclen >= LEN(frc))
		die("too many fallback fonts aaaaaaa\n");
	
	frc[frclen].font = XftFontOpenPattern(fontpattern);
	if (!frc[frclen].font)
		die("XftFontOpenPattern failed seeking fallback font: %s\n", strerror(errno));
	frc[frclen].style = style;
	frc[frclen].unicodep = chr;
	
	*glyph = XftCharIndex(frc[frclen].font, chr);
	
	*xfont = frc[frclen].font;
	frclen++;
	
	FcPatternDestroy(fcpattern);
	FcCharSetDestroy(fccharset);
}

static int cell_fontstyle(const Cell* c) {
	return (c->attrs.weight==1) | (c->attrs.italic)<<1;
}

void cells_to_glyphs(int len, Cell cells[len], Glyph glyphs[len], bool cache) {	
	for (int i=0; i<len; i++) {
		Char chr = cells[i].chr;
		
		// skip blank cells
		if (cells[i].wide==-1 || chr==0 || chr==' ') {
			glyphs[i].chr = chr;
			glyphs[i].font = NULL;
			glyphs[i].glyph = 0;
			continue;
		}
		
		int style = cell_fontstyle(&cells[i]);
		
		if (cache && glyphs[i].chr==chr && glyphs[i].style==style) {
			// do nothing, cached data matches
		} else {
			XftFont* font = fonts[style].font;
			FT_UInt glyph = XftCharIndex(font, chr);
			if (!glyph)
				find_fallback_font(chr, style, &font, &glyph);
			glyphs[i] = (Glyph){
				.font = font,
				.glyph = glyph,
				.chr = chr,
				.style = style,
			};
		}
	}
}
// "_ascent"
//_ascent
 
void fonts_free(void) {
	for (int i=0; i<4; i++) {
		if (fonts[i].font) {
			FcPatternDestroy(fonts[i].pattern);
			XftFontClose(fonts[i].font);
			fonts[i].font = NULL;
		}
	}
	for (int i=0; i<frclen; i++)
		XftFontClose(frc[i].font);
}
