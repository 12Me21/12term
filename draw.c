// Drawing graphics

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "common.h"
#include "x.h"
#include "buffer.h"
#include "font.h"
#include "draw.h"

// atm it doesnt actually matter if this data is correct, it's basically just treated as a cache (so it WILL be used if correct)
static DrawnCell** drawn_chars = NULL;
static int drawn_width = -1;
static int drawn_height = -1;

// cursor
static bool cursor_drawn = false;
static int cursor_x, cursor_y;
static int cursor_width;

static Pixmap pix = None;
static XftDraw* xft_draw = NULL;
static Pixmap under_cursor = None;

// todo: cache the palette colors?
// or perhaps store them as xftcolor internally
static XftColor make_color(Color c) {
	RGBColor rgb;
	if (c.truecolor)
		rgb = c.rgb;
	else {
		int i = c.i;
		if (i>=0 && i<256) {
			rgb = T.palette[i];
		} else if (i == -1)
			rgb = T.foreground;
		else if (i == -3)
			rgb = T.cursor_color;
		else // -2
			rgb = T.background;
	}
	return (XftColor){
		.color = {
			.red = rgb.r*65535/255,
			.green = rgb.g*65535/255,
			.blue = rgb.b*65535/255,
			.alpha = 65535,
		},
	};
}

unsigned long alloc_color(Color c) {
	XftColor x = make_color(c);
	XftColorAllocValue(W.d, W.vis, W.cmap, &x.color, &x);
	return x.pixel;
}

static void clear_background(void) {
	XftDrawSetClip(xft_draw, 0);
	XftDrawRect(xft_draw, (XftColor[]){make_color((Color){.i=-2})}, 0, 0, W.w, W.h);
}

static void init_pixmap(void) {
	if (pix)
		XFreePixmap(W.d, pix);
	pix = XCreatePixmap(W.d, W.win, W.w, W.h, DefaultDepth(W.d, W.scr));
	if (xft_draw)
		XftDrawChange(xft_draw, pix);
	else
		xft_draw = XftDrawCreate(W.d, pix, W.vis, W.cmap);
	
	clear_background();
}

void draw_resize(int width, int height, bool charsize) {
	init_pixmap();
	
	if (drawn_chars) {
		for (int i=0; i<drawn_height; i++)
			free(drawn_chars[i]);
	}
	drawn_height = height;
	drawn_width = width;
	REALLOC(drawn_chars, drawn_height);
	for (int y=0; y<drawn_height; y++) {
		ALLOC(drawn_chars[y], drawn_width);
		for (int x=0; x<drawn_width; x++)
			drawn_chars[y][x] = (DrawnCell){0}; // mreh
	}
	// char size changing
	if (charsize) {
		if (under_cursor)
			XFreePixmap(W.d, under_cursor);
		under_cursor = XCreatePixmap(W.d, W.win, W.cw*2, W.ch, DefaultDepth(W.d, W.scr));
	}
}

static int same_color(XftColor a, XftColor b) {
	return a.color.red==b.color.red && a.color.green==b.color.green && a.color.blue==b.color.blue && a.color.alpha==b.color.alpha;
}

// todo: allow drawing multiple at once for efficiency
static void draw_char_spec(int x, int y, XftGlyphFontSpec* spec, XftColor col) {
	if (!spec)
		return;
	
	Px winx = W.border+x*W.cw;
	Px winy = W.border+y*W.ch;
	
	if (spec) {
		spec->x += winx;
		spec->y += winy;
		XftDrawGlyphFontSpec(xft_draw, &col, spec, 1);
	}
}

static void draw_char_overlays(int x, int y, Cell* c) {
	if (!(c->attrs.underline || c->attrs.strikethrough))
		return;
	int width = c->wide ? 2 : 1;
 
	Px winx = W.border+x*W.cw;
	Px winy = W.border+y*W.ch;
	
	if (c->attrs.underline) {
		XftColor col = make_color(c->attrs.colored_underline ? c->attrs.underline_color : c->attrs.color);
		XftDrawRect(xft_draw, &col, winx, winy+W.font_ascent+1, width*W.cw, c->attrs.underline);
	}
	if (c->attrs.strikethrough) {
		XftDrawRect(xft_draw, (XftColor[]){make_color(c->attrs.color)}, winx, winy+W.font_ascent*2/3, width*W.cw, 1);
	}
}

static void erase_cursor(void) {
	if (!cursor_drawn)
		return;
	XCopyArea(W.d, under_cursor, pix, W.gc,
		0, 0, W.cw*cursor_width, W.ch, // source area
		W.border+W.cw*cursor_x, W.border+W.ch*cursor_y); // dest pos
	cursor_drawn = false;
}

static void draw_cursor(int x, int y) {
	if (cursor_drawn) {
		if (cursor_x==x && cursor_y==y)
			return;
		erase_cursor();
	}
	// todo: adding border each time is a pain. can we specify an origin somehow?
	
	if (x<0)
		x = 0;
	if (x>T.width-1) // this is the only one of these which is expected to happen under normal circumstances
		x = T.width-1;
	if (y<0)
		y = 0;
	if (y>T.height-1)
		y = T.height-1;
	
	Cell temp = T.current->rows[y][x];
	temp.attrs.color = temp.attrs.background;
		
	int width = temp.wide==1 ? 2 : 1;
	
	// save the area underneath the cursor so we can redraw it later
	XCopyArea(W.d, pix, under_cursor, W.gc, W.border+W.cw*x, W.border+W.ch*y, W.cw*width, W.ch, 0, 0);
	
	// this time we do NOT want it to overflow ever
	XftDrawSetClipRectangles(xft_draw, x*W.cw+W.border, y*W.ch+W.border, &(XRectangle){
		.width = W.cw*width,
		.height = W.ch,
	}, 1);
	
	// draw background
	XftDrawRect(xft_draw, (XftColor[]){make_color((Color){.i=-3})}, W.border+W.cw*x, W.border+W.ch*y, W.cw*width, W.ch);
	
	// draw char
	if (temp.chr) {
		XftGlyphFontSpec spec[1];
		int indexs[1];
		int num = make_glyphs(1, spec, &temp, indexs, NULL);
		if (num)
			draw_char_spec(x, y, spec, make_color(temp.attrs.color));
	}
	
	draw_char_overlays(x, y, &temp);
	
	cursor_x = x;
	cursor_y = y;
	cursor_width = width;
	cursor_drawn = true;
}

// todo: what we should do is,
// when a scroll command is issued, keep track of that, and 
// when a redraw happens, shift everything to the final scroll position and then redraw what needs to be done

/*void shift_lines(int src, int dest, int count) {
	erase_cursor();
	XCopyArea(W.d, pix, pix, gc, W.border, W.border+W.ch*src, W.cw*T.width, W.ch*count, W.border, W.border+W.ch*dest);
	if (T.show_cursor)
		draw_cursor(T.c.x, T.c.y);
		}*/

// draw cell backgrounds
static void draw_row_bg(int y) {
	Row row = T.current->rows[y];
	
	XftColor prev_color = make_color(row[0].attrs.background);
	int prev_start = 0;
	int x;
	for (x=1; x<T.width; x++) {
		XftColor bg = make_color(row[x].attrs.background);
		if (!same_color(bg, prev_color)) {
			XftDrawRect(xft_draw, &prev_color, W.border+W.cw*prev_start, W.border+W.ch*y, W.cw*(prev_start-x), W.ch);
			prev_start = x;
			prev_color = bg;
		}
	}
	XftDrawRect(xft_draw, &prev_color, W.border+W.cw*prev_start, W.border+W.ch*y, W.cw*(prev_start-x), W.ch);
}

// draw strikethrough and underlines
static void draw_row_overlays(int y) {
	Row row = T.current->rows[y];
	for (int x=0; x<T.width; x++)
		draw_char_overlays(x, y, &row[x]);
}

static void draw_row(int y) {
	// set clip region to entire row
	// todo: maybe include the border regions too, for chars that overflow their cells
	// but, we do need to remember to clear these regions then
	XftDrawSetClipRectangles(xft_draw, W.border, y*W.ch+W.border, &(XRectangle){
		.width = W.cw*T.width,
		.height = W.ch,
	}, 1);
	
	draw_row_bg(y);
	
	Row row = T.current->rows[y];
	XftGlyphFontSpec specs[T.width];
	int indexs[T.width];
	int num = make_glyphs(T.width, specs, row, indexs, drawn_chars[y]);
	
	for (int i=0; i<num; i++) {
		int x = indexs[i];
		Cell* c = &row[x];
		draw_char_spec(x, y, &specs[i], make_color(c->attrs.color));
	}
	
	draw_row_overlays(y);
	
	T.dirty_rows[y] = false;
	
	if (cursor_y==y)
		cursor_drawn = false;
}

void repaint(void) {
	if (pix)
		XCopyArea(W.d, pix, W.win, W.gc, 0, 0, W.w, W.h, 0, 0);
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

void draw_free(void) {
	// whatever
}

// todo: display characters CENTERED within the cell rather than aligned to the left side.

//so here's a better idea.
// we keep track of what is ACTUALLY rendered on screen
// then when rendering, we just need to umm
// compare to that and only render the cells (and nearby b/c italics)
// which differ.
// it is worth going through a lot of effort to prevent unneeded renders because these are the slowest parts
