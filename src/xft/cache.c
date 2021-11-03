// so i think what we do is
// produce one fontset from the initial pattern
// then, set the bold/italic flags on that when loading individual fonts

#include "../common.h"
#include "xftint.h"
#include "../settings.h"
#include "Xft.h"
#include <X11/extensions/Xrender.h>

GlyphSet glyphset; // use one global glyphset, why not?

typedef struct Bucket {
	Char key;
	GlyphData value[4];
} Bucket;

// number !
#define cache_length 7327
// cache is an array of buckets
static Bucket cache[cache_length] = {0};

#define Font Font_
typedef struct {
	FcPattern* pattern;
	XftFont* font;
	
	FcFontSet* set;
	XftFont** fallback_fonts; // indexes correspond to items in `.set`
} Font;

static Font fonts[4] = {0};

//load one font face
bool load_font(FcPattern* pattern, int style, bool bold, bool italic) {
	Font* f = &fonts[style];
	
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
	/*	int len = 95;
		Char ascii_printable[len];
		for (int i=0; i<len; i++)
			ascii_printable[i] = ' '+i;
		XGlyphInfo extents;
		xft_text_extents(f->font, ascii_printable, len, &extents);
		f->width = ceildiv(extents.xOff, len);*/
		//}
	
	FcResult fcres;
	f->pattern = configured;
	f->set = FcFontSort(NULL, f->pattern, true, NULL, &fcres);
	ALLOC(f->fallback_fonts, f->set->nfont);
	FOR (i, f->set->nfont)
		f->fallback_fonts[i] = NULL;
	
	print("%d fonts in set\n", f->set->nfont);
	//FcFontSetPrint(f->set);
	//f->ascent = f->font->ascent;
	//f->descent = f->font->descent;
	//f->height = f->font->ascent + f->font->descent;
	//f->width = f->height/2; //TEMP!!
	
	return true;
}

int fontset_search(FcFontSet* set, FcPattern* match) {
	FcChar8* match_filename = NULL;
	int match_id = 0;
	FcPatternGetString(match, FC_FILE, 0, &match_filename);
	FcPatternGetInteger(match, FC_INDEX, 0, &match_id);
	FOR (i, set->nfont) {
		FcChar8* filename = NULL;
		int id = 0;
		FcPatternGetString(set->fonts[i], FC_FILE, 0, &filename);
		FcPatternGetInteger(set->fonts[i], FC_INDEX, 0, &id);
		if (!strcmp((utf8*)match_filename, (utf8*)filename) && match_id == id) {
			return i;
		}
	}
	return -1;
}

XftFont* find_char_font(Char chr, int style) {
	Font* f = &fonts[style];
	// check the primary font
	if (FcCharSetHasChar(f->font->charset, chr))
		return f->font;
	// check the alternate fonts (in order)
	FOR (i, f->set->nfont) {
		XftFont* xf = f->fallback_fonts[i];
		if (!xf) // if one isn't loaded yet, we need to give up
			break;
		if (FcCharSetHasChar(xf->charset, chr)) {
			return xf;
		}
	}
	// use fontconfig to find one
	print("searching for char\n");
	FcCharSet* charset = FcCharSetCreate();
	FcPattern* pattern = FcPatternCreate();
	FcCharSetAddChar(charset, chr);
	FcPatternAddCharSet(pattern, FC_CHARSET, charset);
	FcResult res;
	FcPattern* match = FcFontSetMatch(NULL, &f->set, 1, pattern, &res);
	if (match) {
		int i = fontset_search(f->set, match);
		if (i>=0) {
			print("match success. loading font %d\n", i);
			FcConfigSubstitute(0, match, FcMatchPattern);
			pattern_default_substitute(match);
			return XftFontOpenPattern(match);
		}
	}
	return NULL; // couln't find :(
}

GlyphData* cache_lookup(Char chr, uint8_t style) {
	int i = chr % cache_length;
	
	// loop while slot `i` doesn't match
	while (chr != cache[i].key) {
		// found empty slot
		if (cache[i].key == 0) {
			cache[i].key = chr;
			break;
		}
		// todo: choose better offset
		i += 1;
		i %= cache_length;
	}
	
	GlyphData* g = &cache[i].value[style];
	if (!g->exists) {
		// 1: decide which font to use
		XftFont* font = find_char_font(chr, style);
		if (!font)
			return NULL;
		// 2: load the glyph
		if (!load_glyph(font, chr, g))
			return NULL;
	}
	return g;
}

// idea for glyph finding
// for each font in the font set returned by fcfontsort
//  if the font is loaded, check for the glyph
//  otherwise, call fcfontsetmatch, load that font, and add it to the list.
// this SHOULD work assuming the charset match is accurate!!!

// also: we might only need one fontset rather than one for each style.
