#include <errno.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "x.h"
#include "debug.h"
#include "buffer.h"

typedef struct {
	XftFont *font;
	int flags;
	Char unicodep;
} Fontcache;

static Fontcache* frc = NULL;
static int frclen = 0;
static int frccap = 0;

static void reset_clip(void) {
	XftDrawSetClipRectangles(W.draw, W.border, W.border, &(XRectangle){
			.width = W.w-W.border*2,
			.height = W.h-W.border*2,
		}, 1);
}

static XRenderColor get_color(Color c, bool bold) {
	RGBColor rgb;
	if (c.truecolor)
		rgb = c.rgb;
	else {
		int i = c.i;
		if (i>=0 && i<256) {
			if (bold && i<8)
				i+=8;
			rgb = T.palette[i];
		}
		else if (i == -1)
			rgb = T.foreground;
		else if (i == -3)
			rgb = T.cursor_background;
		else
			rgb = T.background;
	}
	return (XRenderColor){
		.red = rgb.r*65535/255,
		.green = rgb.g*65535/255,
		.blue = rgb.b*65535/255,
		.alpha = 65535,
	};
}

// do we need this??
static void alloc_color(XRenderColor* col, XftColor* out) {
	XftColorAllocValue(W.d, W.vis, W.cmap, col, out);
}

static int same_color(XRenderColor a, XRenderColor b) {
	return a.red==b.red && a.green==b.green && a.blue==b.blue && a.alpha==b.alpha;
}

// todo: we should cache this for all the text onscreen mayb
static int xmakeglyphfontspecs(int len, XftGlyphFontSpec specs[len], const Cell cells[len], int x, int y) {
	int winx = W.border+x*W.cw;
	int winy = W.border+y*W.ch;
	
	//unsigned short prevmode = USHRT_MAX;
	Font* font = &W.fonts[0];
	int frcflags = 0;
	int runewidth = W.cw;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = {NULL};
	FcCharSet *fccharset;
	int numspecs = 0;
	
	int xp = winx, yp = winy+font->ascent;
	for (int i=0; i<len; i++) {
		// Fetch rune and mode for current glyph.
		Char rune = cells[i].chr;
		Attrs attrs = cells[i].attrs;
		
		/* Skip dummy wide-character spacing. */
		if (cells[i].wide == -1)
			continue;
		
		/* Determine font for glyph if different from previous glyph. */
		//if (prevmode != mode) {
		//	prevmode = mode;
		frcflags = 0;
		runewidth = W.cw * (cells[i].wide==1 ? 2 : 1);
		if (attrs.italic && attrs.weight==1) {
			frcflags = 3;
		} else if (attrs.italic) {
			frcflags = 2;
		} else if (attrs.weight==1) {
			frcflags = 1;
		}
		font = &W.fonts[frcflags];
		yp = winy + font->ascent; //-1
		//}

		/* Lookup character index with default font. */
		FT_UInt glyphidx = XftCharIndex(W.d, font->match, rune);
		if (glyphidx) {
			specs[numspecs].font = font->match;
			specs[numspecs].glyph = glyphidx;
			specs[numspecs].x = xp;
			specs[numspecs].y = yp;
			xp += runewidth;
			numspecs++;
			continue;
		}

		/* Fallback on font cache, search the font cache for match. */
		int f;
		for (f=0; f<frclen; f++) {
			glyphidx = XftCharIndex(W.d, frc[f].font, rune);
			/* Everything correct. */
			if (glyphidx && frc[f].flags == frcflags)
				goto found;
			/* We got a default font for a not found glyph. */
			if (!glyphidx && frc[f].flags == frcflags && frc[f].unicodep == rune) {
				goto found;
			}
		}
		/* Nothing was found. Use fontconfig to find matching font. */
		if (!font->set)
			font->set = FcFontSort(0, font->pattern, 1, 0, &fcres);
		fcsets[0] = font->set;
		
		fcpattern = FcPatternDuplicate(font->pattern);
		fccharset = FcCharSetCreate();
		
		FcCharSetAddChar(fccharset, rune);
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
		frc[frclen].unicodep = rune;
		
		glyphidx = XftCharIndex(W.d, frc[frclen].font, rune);
		
		f = frclen;
		frclen++;
		
		FcPatternDestroy(fcpattern);
		FcCharSetDestroy(fccharset);
	found:;
		specs[numspecs].font = frc[f].font;
		specs[numspecs].glyph = glyphidx;
		specs[numspecs].x = xp;
		specs[numspecs].y = yp;
		xp += runewidth;
		numspecs++;
	}
	return numspecs;
}

static void draw_char(int x, int y, Cell* c) {
	if (c->wide == -1)
		return;
	XftColor xcol;
	int width = c->wide ? 2 : 1;
	int winx = W.border+x*W.cw;
	int winy = W.border+y*W.ch;
	if (c->chr) {
		XRenderColor fg = get_color(c->attrs.color, c->attrs.weight==1);
		alloc_color(&fg, &xcol);
		XftGlyphFontSpec specs;
		xmakeglyphfontspecs(1, &specs, c, x, y);
		XftDrawGlyphFontSpec(W.draw, &xcol, &specs, 1);
	}
	if (c->attrs.underline)
		XftDrawRect(W.draw, &xcol, winx, winy+W.fonts[0].ascent+1, width*W.cw, 1);
	if (c->attrs.strikethrough)
		XftDrawRect(W.draw, &xcol, winx, winy+W.fonts[0].ascent*2/3, width*W.cw, 1);
	
	if (W.cursor_drawn && W.cursor_x==x && W.cursor_y==y)
		W.cursor_drawn = false;
}

static void erase_cursor(void) {
	if (!W.cursor_drawn)
		return;
	int x = W.cursor_x;
	int y = W.cursor_y;
	int w = W.cursor_width;
	XCopyArea(W.d, W.under_cursor, W.pix, W.gc, 0, 0, W.cw*w, W.ch, W.border+W.cw*x, W.border+W.ch*y);
	W.cursor_drawn = false;
}

static void draw_cursor(int x, int y) {
	if (W.cursor_drawn)
		erase_cursor();
	// todo: adding border each time is a pain. can we specify an origin somehow?
	
	Cell temp = T.current->rows[y][x];
	
	int width = temp.wide==1 ? 2 : 1;
	
	XCopyArea(W.d, W.pix, W.under_cursor, W.gc, W.border+W.cw*x, W.border+W.ch*y, W.cw*width, W.ch, 0, 0);
	
	// this time we do NOT want it to overflow ever
	XftDrawSetClipRectangles(W.draw, x*W.cw+W.border, y*W.ch+W.border, &(XRectangle){
		.width = W.cw*width,
		.height = W.ch,
	}, 1);
	
	XftColor xcol;
	XRenderColor bg = get_color((Color){.i=-3}, 0);
	alloc_color(&bg, &xcol);
	XftDrawRect(W.draw, &xcol, W.border+W.cw*x, W.border+W.ch*y, W.cw*width, W.ch);
	
	temp.attrs.color = temp.attrs.background;
	draw_char(x, y, &temp);
	
	reset_clip();
	
	W.cursor_x = x;
	W.cursor_y = y;
	W.cursor_width = width;
	W.cursor_drawn = true;
}

// todo: instead of calling this immediately,
// keep track of this region until the next redraw
void shift_lines(int src, int dest, int count) {
	erase_cursor();
	XCopyArea(W.d, W.pix, W.pix, W.gc, W.border, W.border+W.ch*src, W.cw*T.width, W.ch*count, W.border, W.border+W.ch*dest);
	if (T.show_cursor)
		draw_cursor(T.c.x, T.c.y);
}

static void draw_row(int y) {
	XftDrawSetClipRectangles(W.draw, W.border, y*W.ch+W.border, &(XRectangle){
		.width = W.cw*T.width,
		.height = W.ch,
	}, 1);
	
	Row row = T.current->rows[y];
	XftColor xcol;
	
	XRenderColor prev_color = get_color(row[0].attrs.background, 0);
	int prev_start = 0;
	int x;
	for (x=1; x<T.width; x++) {
		XRenderColor bg = get_color(row[x].attrs.background, 0);
		if (!same_color(bg, prev_color)) {
			alloc_color(&prev_color, &xcol);
			XftDrawRect(W.draw, &xcol, W.border+W.cw*prev_start, W.border+W.ch*y, W.cw*(prev_start-x), W.ch);
			prev_start = x;
			prev_color = bg;
		}
	}
	alloc_color(&prev_color, &xcol);
	XftDrawRect(W.draw, &xcol, W.border+W.cw*prev_start, W.border+W.ch*y, W.cw*(prev_start-x), W.ch);
	
	for (int x=0; x<T.width; x++) {
		draw_char(x, y, &T.current->rows[y][x]);
	}
	T.dirty_rows[y] = false;
	
	if (W.cursor_y==y)
		W.cursor_drawn = false;
	
	reset_clip();
}

void repaint(void) {
	XCopyArea(W.d, W.pix, W.win, W.gc, 0, 0, W.w, W.h, 0, 0);
}

void draw(void) {
	time_log(NULL);
	for (int y=0; y<T.height; y++) {
		if (T.dirty_rows[y])
			draw_row(y);
	}
	if (T.show_cursor)
		draw_cursor(T.c.x, T.c.y);
	else
		erase_cursor();
	repaint();
	time_log("screen draw");
}

void clear_background(void) {
	XRenderColor bg = get_color((Color){.i=-2}, 0);
	XftColor xcol;
	alloc_color(&bg, &xcol);
	XftDrawSetClip(W.draw, 0);
	XftDrawRect(W.draw, &xcol, 0, 0, W.w, W.h);
	reset_clip();
}

void draw_free(void) {
	for (int i=0; i<frclen; i++)
		XftFontClose(W.d, frc[i].font);
	free(frc);
}

// todo: display characters CENTERED within the cell rather than aligned to the left side.

//so here's a better idea.
// we keep track of what is ACTUALLY rendered on screen
// then when rendering, we just need to umm
// compare to that and only render the cells (and nearby b/c italics)
// which differ.
// it is worth going through a lot of effort to prevent unneeded renders because these are the slowest parts
