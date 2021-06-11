#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "common.h"
#include "x.h"
#include "buffer.h"
#include "font.h"
#include "draw.h"

static Row* drawn_chars = NULL;
static int drawn_width = -1;
static int drawn_height = -1;

void draw_resize(int width, int height) {
	if (drawn_chars) {
		for (int i=0; i<drawn_height; i++)
			free(drawn_chars[i]);
	}
	drawn_height = height;
	drawn_width = width;
	REALLOC(drawn_chars, drawn_height);
	for (int y=0; y<drawn_height; y++) {
		ALLOC(drawn_chars[y], drawn_width);
		for (int x=0; x<drawn_width; x++) {
			drawn_chars[y][x] = (Cell){
				.chr = -1,
			};
		}
	}
}

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
		else // -2
			rgb = T.background;
	}
	return (XRenderColor){
		.red = rgb.r*65535/255,
		.green = rgb.g*65535/255,
		.blue = rgb.b*65535/255,
		.alpha = 65535,
	};
}

static void alloc_color(XRenderColor* col, XftColor* out) {
	// todo: check if it's faster to just do this manually & assume 24bit color.
	XftColorAllocValue(W.d, W.vis, W.cmap, col, out);
}

XftColor make_color(Color c) {
	XftColor out;
	XRenderColor r = get_color(c, false);
	alloc_color(&r, &out);
	return out;
}

static int same_color(XRenderColor a, XRenderColor b) {
	return a.red==b.red && a.green==b.green && a.blue==b.blue && a.alpha==b.alpha;
}

static void draw_char_spec(int x, int y, Cell* c, XftGlyphFontSpec* spec) {
	if (c->wide == -1)
		return;
	XftColor xcol;
	if (spec) {
		XRenderColor fg = get_color(c->attrs.color, c->attrs.weight==1);
		alloc_color(&fg, &xcol);
		XftDrawGlyphFontSpec(W.draw, &xcol, spec, 1);
	}
	
	int width = c->wide ? 2 : 1;
	int winx = W.border+x*W.cw;
	int winy = W.border+y*W.ch;
	
	if (c->attrs.underline)
		XftDrawRect(W.draw, &xcol, winx, winy+W.font_ascent+1, width*W.cw, 1);
	if (c->attrs.strikethrough)
		XftDrawRect(W.draw, &xcol, winx, winy+W.font_ascent*2/3, width*W.cw, 1);
}

static void draw_char(int x, int y, Cell* c) {
	if (c->wide == -1)
		return;
	
	if (c->chr) {
		XftGlyphFontSpec spec;
		xmakeglyphfontspecs(1, &spec, c, x, y);
		draw_char_spec(x, y, c, &spec);
	} else {
		draw_char_spec(x, y, c, NULL);
	}
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
	if (W.cursor_drawn) {
		if (W.cursor_x==x && W.cursor_y==y)
			return;
		erase_cursor();
	}
	// todo: adding border each time is a pain. can we specify an origin somehow?
	
	Cell temp = T.current->rows[y][x];
	
	int width = temp.wide==1 ? 2 : 1;
	
	XCopyArea(W.d, W.pix, W.under_cursor, W.gc, W.border+W.cw*x, W.border+W.ch*y, W.cw*width, W.ch, 0, 0);
	
	// this time we do NOT want it to overflow ever
	XftDrawSetClipRectangles(W.draw, x*W.cw+W.border, y*W.ch+W.border, &(XRectangle){
		.width = W.cw*width,
		.height = W.ch,
	}, 1);
	
	XftColor xcol = make_color((Color){.i=-3});
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

/*static bool cell_needs_redraw(int x, int y) {
	Cell* current = &drawn_chars[y][x];
	Cell* new = T.current->rows[y][x];
	if (current->attrs.all != new->attrs.all)
		return true;
	if (new->attrs.color.truecolor) {
		if (current->attrs.color.truecolor) 
			} else {
				
			}
			}*/

static void draw_row_bg(int y) {
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
}

static void draw_row(int y) {
	// set clip region to entire row
	XftDrawSetClipRectangles(W.draw, W.border, y*W.ch+W.border, &(XRectangle){
		.width = W.cw*T.width,
		.height = W.ch,
	}, 1);
	
	draw_row_bg(y);
	
	Row row = T.current->rows[y];
	XftGlyphFontSpec specs[T.width];
	int num = xmakeglyphfontspecs(T.width, specs, row, 0, y);
	// draw text
	
	for (int x=0; x<num; x++) {
		draw_char_spec(x, y, &row[x], &specs[x]);
		//drawn_chars[y][x] = T.current->rows[y][x];
	}
	T.dirty_rows[y] = false;
	
	if (W.cursor_y==y)
		W.cursor_drawn = false;
	
	reset_clip();
}

void repaint(void) {
	if (W.pix != W.win)
		XCopyArea(W.d, W.pix, W.win, W.gc, 0, 0, W.w, W.h, 0, 0);
}

void draw(void) {
	time_log(NULL);
	print("dirty rows: [");
	for (int y=0; y<T.height; y++) {
		print("%c", ".#"[T.dirty_rows[y]]);
	}
	print("] ");
	for (int y=0; y<T.height; y++) {
		if (T.dirty_rows[y])
			draw_row(y);
	}
	// todo: optimize this to avoid extra redraws I guess
	if (T.show_cursor)
		draw_cursor(T.c.x, T.c.y);
	else
		erase_cursor();
	// todo: definitely avoid extra repaints
	repaint();
	time_log("redraw");
}

void clear_background(void) {
	XftColor xcol = make_color((Color){.i=-2});
	XftDrawSetClip(W.draw, 0);
	XftDrawRect(W.draw, &xcol, 0, 0, W.w, W.h);
	reset_clip();
}

void draw_free(void) {
}

// todo: display characters CENTERED within the cell rather than aligned to the left side.

//so here's a better idea.
// we keep track of what is ACTUALLY rendered on screen
// then when rendering, we just need to umm
// compare to that and only render the cells (and nearby b/c italics)
// which differ.
// it is worth going through a lot of effort to prevent unneeded renders because these are the slowest parts

