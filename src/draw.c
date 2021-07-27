// Drawing graphics

// idea: rather than having one framebuffer, have separate ones for each row
// that way, it's easy to shift rows around (i.e. when scrolling) and then copy that data onto the window in whatever order

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "common.h"
#include "x.h"
#include "buffer.h"
#include "font.h"
#include "draw.h"
#include "draw2.h"
#include "event.h"

typedef struct DrawRow {
	DrawnCell* glyphs;
	Cell* cells;
	Pixmap pix;
	XftDraw* draw;
} DrawRow;

// atm it doesnt actually matter if this data is correct, it's basically just treated as a cache (so it WILL be used if correct)
DrawRow* rows = NULL;
// todo: it's kinda messy having the size stored in multiple places.
static int drawn_width = -1, drawn_height = -1;

static Cell* blank_row = NULL;

// cursor
static Pixmap cursor_pix = None;
static XftDraw* cursor_draw = NULL;
static int cursor_width;
static int cursor_y;

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

void draw_resize(int width, int height, bool charsize) {
	if (rows) {
		for (int i=0; i<drawn_height; i++) {
			FREE(rows[i].glyphs);
			FREE(rows[i].cells);
			XFreePixmap(W.d, rows[i].pix);
			XftDrawDestroy(rows[i].draw);
		}
	}
	drawn_height = height;
	drawn_width = width;
	REALLOC(rows, height);
	for (int y=0; y<drawn_height; y++) {
		ALLOC(rows[y].glyphs, drawn_width);
		ALLOC(rows[y].cells, drawn_width);
		for (int x=0; x<drawn_width; x++) {
			rows[y].glyphs[x] = (DrawnCell){0}; // mreh
			rows[y].cells[x] = (Cell){0}; //ehnnnn
		}
		rows[y].pix = XCreatePixmap(W.d, W.win, W.w, W.ch, DefaultDepth(W.d, W.scr));
		rows[y].draw = XftDrawCreate(W.d, rows[y].pix, W.vis, W.cmap);
	}
	
	REALLOC(blank_row, drawn_width);
	for (int x=0; x<drawn_width; x++)
		blank_row[x] = (Cell){.attrs={.background={.i=-2}}};
	
	// char size changing
	if (charsize) {
		if (cursor_pix)
			XFreePixmap(W.d, cursor_pix);
		cursor_pix = XCreatePixmap(W.d, W.win, W.cw*2, W.ch, DefaultDepth(W.d, W.scr));
		if (cursor_draw)
			XftDrawChange(cursor_draw, cursor_pix);
		else
			cursor_draw = XftDrawCreate(W.d, cursor_pix, W.vis, W.cmap);
	}
}

static int same_color(XftColor a, XftColor b) {
	return a.color.red==b.color.red && a.color.green==b.color.green && a.color.blue==b.color.blue && a.color.alpha==b.color.alpha;
}

// todo: allow drawing multiple at once for efficiency
static void draw_char_spec(XftDraw* draw, Px x, XftGlyphFontSpec* spec, XftColor col) {
	if (!spec)
		return;
	
	if (spec) {
		spec->x += x;
		XftDrawGlyphFontSpec(draw, &col, spec, 1);
	}
}

static void draw_char_overlays(XftDraw* draw, Px winx, Cell c) {
	int underline = c.attrs.underline;
	if (!(underline || c.attrs.strikethrough || c.attrs.link))
		return;
	Color underline_color = c.attrs.colored_underline ? c.attrs.underline_color : c.attrs.color;
	int width = c.wide ? 2 : 1;
	
	// display a blue underline on hyperlinks (if they don't already have an underline)
	if (c.attrs.link && !underline) {
		underline = 1;
		underline_color = (Color){.i=8+4}; //todo: maybe make a special palette entry for this purpose?
	}
	
	if (underline) {
		XftColor col = make_color(underline_color);
		XftDrawRect(draw, &col, winx, W.font_ascent+1, width*W.cw, underline);
	}
	if (c.attrs.strikethrough) {
		XftDrawRect(draw, (XftColor[]){make_color(c.attrs.color)}, winx, W.font_ascent*2/3, width*W.cw, 1);
	}
}

static void draw_cursor(int x, int y) {
	//xim_spot(T.c.x, T.c.y);
	
	x = limit(x, 0, drawn_width); // not -1
	y = limit(y, 0, drawn_height-1);
	
	Row row = T.current->rows[y];
	Cell temp;
	if (row)
		temp = row[x];
	else
		temp = (Cell){0};
	temp.attrs.color = temp.attrs.background;
		
	int width = temp.wide==1 ? 2 : 1;
	
	// draw background
	XftDrawRect(cursor_draw, (XftColor[]){make_color((Color){.i=-3})}, 0, 0, W.cw*width, W.ch);
	
	// draw char
	if (temp.chr) {
		XftGlyphFontSpec spec[1];
		int indexs[1];
		int num = make_glyphs(1, spec, &temp, indexs, NULL);
		if (num)
			draw_char_spec(cursor_draw, 0, spec, make_color(temp.attrs.color));
	}
	
	draw_char_overlays(cursor_draw, 0, temp);
	
	cursor_width = width;
}

static void copy_row_data(int src, int dest) {
	rows[dest] = rows[src];
}

void draw_copy_rows(int src, int dest, int num) {
	return;
	print("copy %d rows, from %d to %d\n", num, src, dest);
	if (num<=0)
		return;
	if (dest>src) {
		for (int i=num-1; i>=0; i--)
			copy_row_data(src+i, dest+i);
	} else if (dest<src) {
		for (int i=0; i<num; i++)
			copy_row_data(src+i, dest+i);
	} else
		return;
	//XCopyArea(W.d, pix, pix, W.gc, W.border, W.border+W.ch*src, W.cw*T.width, W.ch*num, W.border, W.border+W.ch*dest);
}

static bool draw_row(int y, Row row) {
	if (!memcmp(row, rows[y].cells, sizeof(Cell)*drawn_width))
		return false;
	
	memcpy(rows[y].cells, row, drawn_width*sizeof(Cell));
	
	if (row==blank_row) {
		XftDrawRect(rows[y].draw, (XftColor[]){make_color((Color){.truecolor=true,.rgb=T.background})}, 0, 0, W.w, W.ch );
		return true;
	}
	
	// draw left border background
	XftDrawRect(rows[y].draw, (XftColor[]){make_color((Color){.i=-2})}, 0, 0, W.border, W.ch);
	// draw cell backgrounds
	XftColor prev_color = make_color(row[0].attrs.background);
	int prev_start = 0;
	int x;
	for (x=1; x<drawn_width; x++) {
		XftColor bg = make_color(row[x].attrs.background);
		if (!same_color(bg, prev_color)) {
			XftDrawRect(rows[y].draw, &prev_color, W.border+W.cw*prev_start, 0, W.cw*(prev_start-x-1), W.ch);
			prev_start = x;
			prev_color = bg;
		}
	}
	XftDrawRect(rows[y].draw, &prev_color, W.border+W.cw*prev_start, 0, W.cw*(prev_start-x-1), W.ch);
	// draw right border background
	XftDrawRect(rows[y].draw, (XftColor[]){make_color((Color){.i=-2})}, W.border+W.cw*drawn_width, 0, W.border, W.ch);
	
	// draw text
	XftGlyphFontSpec specs[drawn_width];
	int indexs[drawn_width];
	int num = make_glyphs(drawn_width, specs, row, indexs, rows[y].glyphs);
	
	for (int i=0; i<num; i++) {
		int x = indexs[i];
		draw_char_spec(rows[y].draw, W.border+x*W.cw, &specs[i], make_color(row[x].attrs.color));
	}
	
	// draw strikethrough and underlines
	for (int x=0; x<drawn_width; x++)
		draw_char_overlays(rows[y].draw, W.border+x*W.cw, row[x]);
	
	return true;
}

static void paint_row(int y) {
	XCopyArea(W.d, rows[y].pix, W.win, W.gc, 0, 0, W.w, W.ch, 0, W.border+W.ch*y);
	if (T.c.y == y && T.show_cursor) {
		XCopyArea(W.d, cursor_pix, W.win, W.gc, 0, 0, W.cw*cursor_width, W.ch, W.border+T.c.x*W.cw, W.border+W.ch*y);
		cursor_y = y;
	}
}

void draw(bool repaint_all) {
	time_log(NULL);
	print("dirty rows: [");
	draw_cursor(T.c.x, T.c.y); // todo: do we need this every time?
	paint_row(cursor_y); // todo: not ideal ehh
	if (repaint_all) {
		// todo: erase the top/bottom borders here?
	}
	for (int y=0; y<drawn_height; y++) {
		Row row = blank_row;
		int ry = y;
		if (T.current == &T.buffers[0]) {
			ry = y-T.scrollback.pos;
			if (ry>=0 && ry<drawn_height) {
				// row is in screen buffer
				row = T.current->rows[ry];
			} else if (ry<0 && T.scrollback.lines+ry>=0) {
				// row is in scrollback
				row = T.scrollback.rows[T.scrollback.lines+ry];
			}
		} else {
			row = T.current->rows[y];
		}
		bool paint = false;
		if (draw_row(y, row)) {
			paint = true;
			print(row==blank_row ? "~" : "#");
		} else {
			print(".");
		}
		if (repaint_all || paint || T.c.y == y)
			paint_row(y);
	}
	print("] ");
	time_log("redraw");
}

void draw_free(void) {
	// whatever
}

// call this when changing palette etc.
void dirty_all(void) {
	for (int y=0; y<drawn_height; y++) {
		for (int x=0; x<drawn_width; x++) {
			rows[y].glyphs[x] = (DrawnCell){0}; // mreh
			rows[y].cells[x] = (Cell){0}; //ehnnnn
		}
	}
}

// todo: display characters CENTERED within the cell rather than aligned to the left side.
