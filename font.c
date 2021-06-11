#include <errno.h>
#include <math.h>
#include <X11/Xft/Xft.h>
//#include <hb-ft.h>

#include "common.h"
#include "font.h"
#include "buffer.h"
#include "x.h"

#define Font Font_
typedef struct {
	Px width, height;
	Px ascent, descent;
	Px lbearing, rbearing;
	
	bool badslant, badweight;
	XftFont* match;
	//hb_font_t* hb_font;
	FcFontSet* set;
	FcPattern* pattern;
} Font;

Font fonts[4]; // normal, bold, italic, bold+italic

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
	if (XftPatternGetInteger(pattern, "slant", 0, &wantattr) == XftResultMatch) {
		if (XftPatternGetInteger(f->match->pattern, "slant", 0, &haveattr)!=XftResultMatch || haveattr<wantattr) {
			f->badslant = true;
		}
	}
	if (XftPatternGetInteger(pattern, "weight", 0, &wantattr) == XftResultMatch) {
		if (XftPatternGetInteger(f->match->pattern, "weight", 0, &haveattr)!=XftResultMatch || haveattr != wantattr) {
			f->badweight = true;
		}
	}
	
	const char ascii_printable[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
	int len = strlen(ascii_printable);
	XGlyphInfo extents;
	XftTextExtentsUtf8(W.d, f->match, (const FcChar8*)ascii_printable, len, &extents);
	f->set = NULL;
	f->pattern = configured;
	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance_width;
	f->height = f->ascent + f->descent;
	f->width = ceildiv(extents.xOff, len);
	
	//FT_Face face = XftLockFace(f->match);
	//f->hb_font = hb_ft_font_create(face, NULL);
	
	return 0;
}

void init_fonts(const char* fontstr, double fontsize) {
	FcPattern* pattern;
		
	if (fontstr[0] == '-')
		pattern = XftXlfdParse(fontstr, False, False);
	else
		pattern = FcNameParse((const FcChar8*)fontstr);
	
	//if (!pattern)
	//	die("can't open font %s\n", fontstr);
	
	
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

typedef struct {
	XftFont* font;
	int flags;
	Char unicodep;
	//hb_font_t* hb_font;
} Fontcache;

static Fontcache* frc = NULL;
static int frclen = 0;
static int frccap = 0;

// todo:
static bool can_connect(const Cell* a, const Cell* b) {
	return true;
}

/*hb_font_t* hbfindfont(XftFont* font) { //temp
	for (int i=0; i<4; i++) {
		if (fonts[i].match == font)
			return fonts[i].hb_font;
	}
	for (int i=0; i<frclen; i++) {
		if (frc[i].font == font)
			return frc[i].hb_font;
	}
	return NULL;
	}*/

/*static void hbtransformsegment(XftFont* xfont, const Cell* cells, hb_codepoint_t* codepoints, int start, int length) {
	hb_font_t* font = hbfindfont(xfont);
	if (font == NULL)
		return;
	
	hb_buffer_t* buffer = hb_buffer_create();
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	
	for (int i=start; i<start+length; i++) {
		Char chr = cells[i].chr;
		if (cells[i].wide==-1 || cells[i].chr==0)
			chr = ' '; // really?
		hb_buffer_add_codepoints(buffer, (hb_codepoint_t[]){chr}, 1, 0, 1);
	}
	
	hb_shape(font, buffer, NULL, 0);
	
	hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, NULL);

	for (int i=0; i<length; i++)
		codepoints[start+i] = info[i].codepoint;

	hb_buffer_destroy(buffer);
	}*/

// todo: we should cache this for all the text onscreen mayb
int xmakeglyphfontspecs(int len, XftGlyphFontSpec specs[len], /*const*/ Cell cells[len], int x, int y) {
	int winx = W.border+x*W.cw;
	int winy = W.border+y*W.ch;
	
	//unsigned short prevmode = USHRT_MAX;
	Font* font = &fonts[0];
	int frcflags = 0;
	int chrwidth = W.cw;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = {NULL};
	FcCharSet *fccharset;
	int numspecs = 0;
	
	int xp = winx, yp = winy+font->ascent;
	for (int i=0; i<len; i++) {
		// Fetch chr and mode for current glyph.
		Char chr = cells[i].chr;
		Attrs attrs = cells[i].attrs;
		
		/* Skip dummy wide-character spacing. */
		if (cells[i].wide == -1)
			continue;
		
		chrwidth = W.cw * (cells[i].wide==1 ? 2 : 1);
				
		// empty cells
		if (chr == 0) {
			chr = ' ';
			//xp += chrwidth;
			//continue;
		}
		
		/* Determine font for glyph if different from previous glyph. */
		//if (prevmode != mode) {
		//	prevmode = mode;
		frcflags = 0;

		if (attrs.italic && attrs.weight==1) {
			frcflags = 3;
		} else if (attrs.italic) {
			frcflags = 2;
		} else if (attrs.weight==1) {
			frcflags = 1;
		}
		font = &fonts[frcflags];
		yp = winy + font->ascent; //-1
		//}
		
		/* Lookup character index with default font. */
		FT_UInt glyphidx = XftCharIndex(W.d, font->match, chr);
		if (glyphidx) {
			specs[numspecs].font = font->match;
			specs[numspecs].glyph = glyphidx;
			specs[numspecs].x = xp;
			specs[numspecs].y = yp;
			xp += chrwidth;
			numspecs++;
			continue;
		}
		
		/* Fallback on font cache, search the font cache for match. */
		int f;
		for (f=0; f<frclen; f++) {
			glyphidx = XftCharIndex(W.d, frc[f].font, chr);
			/* Everything correct. */
			if (glyphidx && frc[f].flags == frcflags)
				goto found;
			/* We got a default font for a not found glyph. */
			if (!glyphidx && frc[f].flags == frcflags && frc[f].unicodep == chr) {
				goto found;
			}
		}
		/* Nothing was found. Use fontconfig to find matching font. */
		if (!font->set)
			font->set = FcFontSort(0, font->pattern, 1, 0, &fcres);
		fcsets[0] = font->set;
		
		fcpattern = FcPatternDuplicate(font->pattern);
		fccharset = FcCharSetCreate();
		
		FcCharSetAddChar(fccharset, chr);
		FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
		FcPatternAddBool(fcpattern, FC_SCALABLE, 1);
		
		FcConfigSubstitute(0, fcpattern, FcMatchPattern);
		FcDefaultSubstitute(fcpattern);
		
		fontpattern = FcFontSetMatch(0, fcsets, 1, fcpattern, &fcres);
		
		/* Allocate memory for the new cache entry. */
		if (frclen >= frccap) {
			frccap += 16;
			frc = realloc(frc, frccap * sizeof(Fontcache));
		}
		
		frc[frclen].font = XftFontOpenPattern(W.d, fontpattern);
		if (!frc[frclen].font)
			die("XftFontOpenPattern failed seeking fallback font: %s\n",
				strerror(errno));
		frc[frclen].flags = frcflags;
		frc[frclen].unicodep = chr;
		
		//FT_Face face = XftLockFace(frc[frclen].font);
		//frc[frclen].hb_font = hb_ft_font_create(face, NULL);
		
		glyphidx = XftCharIndex(W.d, frc[frclen].font, chr);
		
		f = frclen;
		frclen++;
		
		FcPatternDestroy(fcpattern);
		FcCharSetDestroy(fccharset);
	found:;
		specs[numspecs].font = frc[f].font;
		specs[numspecs].glyph = glyphidx;
		specs[numspecs].x = xp;
		specs[numspecs].y = yp;
		xp += chrwidth;
		numspecs++;
	}
	
	/*if (W.ligatures) {
		
		int cstart = 0;
		int sstart = 0;
		
		hb_codepoint_t codepoints[len];
		
		int s = 0;
		int i = 0;
		
		for (; i<len; i++) {
			if (cells[i].wide==-1)
				continue;
			if (specs[sstart].font==specs[s].font) {
				
			} else {
				hbtransformsegment(specs[sstart].font, cells, codepoints, cstart, i-cstart);
				cstart = i;
				sstart = s;
			}
			s++;
		}
		
		if (numspecs)
			hbtransformsegment(specs[sstart].font, cells, codepoints, cstart, i-cstart);
		
		
		if (numspecs) {
			for (int i=0, s=0; i < len; i++) {
				if (cells[i].wide==-1)
					continue;
				
				if (codepoints[i] != specs[s].glyph)
					cells[i].ligature = true;
				
				specs[s++].glyph = codepoints[i];
			}
		}
	 } */
	
	return numspecs;
}

void fonts_free(void) {
	// todo: free HB fonts properly
	for (int i=0; i<4; i++) {
		FcPatternDestroy(fonts[i].pattern);
		XftFontClose(W.d, fonts[i].match);
	}
	for (int i=0; i<frclen; i++)
		XftFontClose(W.d, frc[i].font);
	free(frc);
}
