// so i think what we do is
// produce one fontset from the initial pattern
// then, set the bold/italic flags on that when loading individual fonts

#include "../common.h"
#include "xftint.h"
#include "../settings.h"
#include "Xft.h"
#include <X11/extensions/Xrender.h>

XftFormat xft_formats[PictStandardNUM] = {0};

static void init_format(int type) {
	XftFormat* f = &xft_formats[type];
	if (!f->format) {
		f->format = XRenderFindStandardFormat(W.d, type);
	}
	
	if (f->glyphset) {
		XRenderFreeGlyphSet(W.d, f->glyphset);
	}
	f->glyphset = XRenderCreateGlyphSet(W.d, f->format);
	f->next_glyph = 0;
}

static void init_formats(void) {
	init_format(PictStandardARGB32);
	init_format(PictStandardA8);
	init_format(PictStandardA1);
}

typedef struct Bucket {
	Char key;
	GlyphData value[4];
} Bucket;

// number !
#define cache_length 14327
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
static bool load_font(FcPattern* pattern, int style, bool bold, bool italic) {
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

void fonts_free(void) {
	// empty the cache:
	FOR (i, cache_length) {
		if (cache[i].key) {
			cache[i].key = 0;
			FOR (j, 4) {
				if (cache[i].value[j].type==2)
					XRenderFreePicture(W.d, cache[i].value[j].picture);
				cache[i].value[j].type = 0;
			}
		}
	}
	// free fonts
	FOR (i, 4) {
		Font* f = &fonts[i];
		if (f->font) {
			FcPatternDestroy(f->pattern);
			f->font = NULL;
			if (f->set) {
				FcFontSetDestroy(f->set);
				f->set = NULL;
				free(f->fallback_fonts);
			}
		}
	}
}

// This frees any existing fonts and loads new ones, based on `fontstr`.
// it also sets `W.cw` and `W.ch`.
void load_fonts(const utf8* fontstr, double fontsize) {
	print("loading pattern: %s\n", fontstr);
	
	init_formats();
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
		if (!load_font(pattern, i, i&1, i&2))
			die("failed to load font");
		time_log("loaded font");
	}
	
	// calculate char cell size
	int width = 0;
	int count = 0;
	for (Char i=' '; i<='~'; i++) {
		GlyphData* d = cache_lookup(i, 0);
		width += d->metrics.xOff;
		count++;
	}
	W.cw = (width+count/2) / count; // average width
	
	FcPatternDestroy(pattern);
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
	if (DEBUG.cache)
		print("searching for char\n");
	FcCharSet* charset = FcCharSetCreate();
	FcPattern* pattern = FcPatternDuplicate(f->pattern);
	FcCharSetAddChar(charset, chr);
	FcPatternAddCharSet(pattern, FC_CHARSET, charset);
	FcResult res;
	FcPattern* match = FcFontSetMatch(NULL, &set, 1, pattern, &res);
	FcPatternDestroy(pattern);
	FcCharSetDestroy(charset);
	
	if (match) {
		int i = fontset_search(set, match);
		if (i>=0) {
			if (DEBUG.cache)
				print("match success. loading font %d\n", i);
			//pattern_default_substitute(match);
			//FcConfigSubstitute(NULL, match, FcMatchFont); // changed this from FcMatchPattern to FcMatchFont and that helped with some memory leaks????
			//pattern_default_substitute(match);
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

#define cache_ascii 95
#define cache_other (cache_length-cache_ascii)

// todo: make sure we actually fill in the cache
// even if loading fails
// so we don't try to load invalid glyphs every time
GlyphData* cache_lookup(Char chr, uint8_t style) {
	int i;
	if (chr>=' ' && chr<='~') {
		i = chr - ' ';
	} else {
		i = cache_ascii + chr % cache_other;
		
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
			if (i >= cache_length)
				i -= cache_other;
			collisions++;
			if (collisions >= cache_length) {
				print("CACHE IS FULL!\n");
				return NULL;
			}
		}
		if (collisions)
			if (DEBUG.cache)
				print("cache collisions: %d\n", collisions);
	}
	
	GlyphData* g = &cache[i].value[style];
	if (!g->type) {
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

// also: we might only need one fontset rather than one for each style.


// todo: 
// so there's a control seq which allows changing the current font
// really we shouldn't free ALL fonts because the fallback fonts will likely remain the same
// but this raises a bigger issue:
// should the fallback list be stored separately from the main font?

// ideally the fallback list is derived from the main font, but
// that means that the "main font" in practice is actually a pattern like "cascadia code, twemoji, deja vu"

// but this sucks because it means you need to pass this full list when changing the font

// so idk..
// maybe append the fallbacks somehow (NOT by just appending strings though)
