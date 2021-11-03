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
// todo: more error checking here
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
	
	f->font = XftFontOpenPattern(match);
	
	f->pattern = configured;
	
	// todo: make this less of a hack
	if (!style) {
		W.ch = f->font->ascent + f->font->descent;
		W.font_baseline = f->font->ascent;
	}
	
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
	if (!f->set) {
		FcResult result;
		f->set = FcFontSort(NULL, f->pattern, true, NULL, &result);
		ALLOC(f->fallback_fonts, f->set->nfont);
		FOR (i, f->set->nfont)
			f->fallback_fonts[i] = NULL;
	}
	FcFontSet* set = f->set; //fonts[0].set;
	// check the primary font
	if (FcCharSetHasChar(f->font->charset, chr))
		return f->font;
	// check the alternate fonts (in order)
	FOR (i, set->nfont) {
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
	FcPattern* pattern = FcPatternDuplicate(f->pattern);
	FcCharSetAddChar(charset, chr);
	FcPatternAddCharSet(pattern, FC_CHARSET, charset);
	FcResult res;
	FcPattern* match = FcFontSetMatch(NULL, &set, 1, pattern, &res);
	
	if (match) {
		int i = fontset_search(set, match);
		if (i>=0) {
			print("match success. loading font %d\n", i);
			FcConfigSubstitute(0, match, FcMatchPattern);
			pattern_default_substitute(match);
			XftFont* xf = XftFontOpenPattern(match);
			f->fallback_fonts[i] = xf;
			return xf;
		} else {
			// this should never happen
			print("loaded unknown fallback font?\n");
		}
	}
	return NULL; // couln't find :(
}

// todo: make sure we actually fill in the cache
// even if loading fails
// so we don't try to load invalid glyphs every time
GlyphData* cache_lookup(Char chr, uint8_t style) {
	int i = chr % cache_length;
	
	int collisions = 0;
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
		collisions++;
	}
	if (collisions)
		print("cache collisions: %d\n", collisions);
	
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
