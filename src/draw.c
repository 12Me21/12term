// Drawing graphics

#include <X11/Xlib.h>
#include "xft/Xft.h"

#include "common.h"
#include "x.h"
#include "buffer.h"
#include "font.h"
#include "draw.h"
#include "draw2.h"
#include "event.h"

typedef struct DrawRow {
	// cache of the glyphs and cells
	Cell* cells;
	Glyph* glyphs;
	// framebuffer
	Pixmap pix;
	XftDraw* draw;
	// to force a redraw 
	bool redraw;
} DrawRow;

static DrawRow* rows = NULL;

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

// these are only used to track the old size in this function
static int drawn_width = -1, drawn_height = -1;
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
	for (int y=0; y<T.height; y++) {
		ALLOC(rows[y].glyphs, T.width);
		ALLOC(rows[y].cells, T.width);
		for (int x=0; x<T.width; x++) {
			rows[y].glyphs[x] = (Glyph){0}; // mreh
			rows[y].cells[x] = (Cell){0}; //ehnnnn
		}
		rows[y].pix = XCreatePixmap(W.d, W.win, W.w, W.ch, DefaultDepth(W.d, W.scr));
		rows[y].draw = XftDrawCreate(W.d, rows[y].pix, W.vis, W.cmap);
		rows[y].redraw = true;
	}
	
	REALLOC(blank_row, T.width);
	for (int x=0; x<T.width; x++)
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

// todo: allow drawing multiple at once for efficiency?
static void draw_glyph(XftDraw* draw, Px x, Px y, Glyph g, XftColor col, int w) {
	if (!g.font)
		return;
	Picture src = XftDrawSrcPicture(draw, &col);
	XftGlyphRender1(XftDrawDisplay(draw), PictOpOver, src, g.font, XftDrawPicture(draw), 0, 0, x+g.x, y+g.y, g.glyph, W.cw*w);
	//XftGlyphFontSpecRender(XftDrawDisplay(draw), PictOpOver, src, XftDrawPicture(draw), 0, 0, spec, 1);
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
	x = limit(x, 0, T.width); // not -1
	y = limit(y, 0, T.height-1);
	
	xim_spot(x, y);
	
	Row row = T.current->rows[y];
	Cell temp;
	if (row && x<T.width)
		temp = row[x];
	else
		temp = (Cell){0};
	temp.attrs.color = temp.attrs.background;
		
	int width = temp.wide==1 ? 2 : 1;
	
	// draw background
	XftDrawRect(cursor_draw, (XftColor[]){make_color((Color){.i=-3})}, 0, 0, W.cw*width, W.ch);
	
	// draw char
	if (temp.chr) {
		Glyph spec[1];
		cells_to_glyphs(1, &temp, spec, false);
		draw_glyph(cursor_draw, 0, 0, spec[0], make_color(temp.attrs.color), width);
	}
	
	draw_char_overlays(cursor_draw, 0, temp);
	
	cursor_width = width;
}

static void rotate(int amount, int length, DrawRow start[length]) {
	while (amount<0)
		amount += length;
	amount %= length;
	int a=0;
	int b=0;
	
	for (int i=0; i<length; i++) {
		b = (b+amount) % length;
		if (b==a)
			b = ++a;
		if (b!=a) {
			DrawRow temp;
			temp = start[a];
			start[a] = start[b];
			start[b] = temp;
		}
	}
}

// rotate rows around.
// if `screen_space` is set, don't adjust for scrollback position
void draw_rotate_rows(int y1, int y2, int amount, bool screen_space) {
	if (!screen_space && T.current==&T.buffers[0]) {
		y1 -= T.history.scroll;
		y2 -= T.history.scroll;
	}
	y1 = limit(y1, 0, T.height-1);
	y2 = limit(y2, y1, T.height);
	if (y2<=y1)
		return;
	rotate(amount, y2-y1, &rows[y1]);
	for (int y=y1; y<y2; y++)
		rows[y].redraw = true;
}

static bool draw_row(int y, Row row) {
	if (!memcmp(row, rows[y].cells, sizeof(Cell)*T.width))
		return false;
	
	memcpy(rows[y].cells, row, T.width*sizeof(Cell));
	
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
	for (x=1; x<T.width; x++) {
		XftColor bg = make_color(row[x].attrs.background);
		if (!same_color(bg, prev_color)) {
			XftDrawRect(rows[y].draw, &prev_color, W.border+W.cw*prev_start, 0, W.cw*(prev_start-x-1), W.ch);
			prev_start = x;
			prev_color = bg;
		}
	}
	XftDrawRect(rows[y].draw, &prev_color, W.border+W.cw*prev_start, 0, W.cw*(prev_start-x-1), W.ch);
	// draw right border background
	XftDrawRect(rows[y].draw, (XftColor[]){make_color((Color){.i=-2})}, W.border+W.cw*T.width, 0, W.border, W.ch);
	
	// draw text
	Glyph* specs = rows[y].glyphs;
	cells_to_glyphs(T.width, row, specs, true);
	
	for (int i=0; i<T.width; i++) {
		if (specs[i].font)
			draw_glyph(rows[y].draw, W.border+i*W.cw, 0, specs[i], make_color(row[i].attrs.color), row[i].wide==1 ? 2 : 1);
	}
	
	// draw strikethrough and underlines
	for (int x=0; x<T.width; x++)
		draw_char_overlays(rows[y].draw, W.border+x*W.cw, row[x]);
	
	return true;
}

static int row_displayed_at(int y) {
	if (T.current == &T.buffers[0])
		return y-T.history.scroll;
	return y;
}

void copy_cursor_part(Px x, Px y, Px w, Px h, int cx, int cy) {
	XCopyArea(W.d, cursor_pix, W.win, W.gc,
		x, y, w, h, W.border+cx*W.cw+x, W.border+W.ch*cy+y);
}

// todo: vary thickness of cursors and lines based on font size

// todo: keep better track of where cursor is rendered
static void paint_row(int y) {
	XCopyArea(W.d, rows[y].pix, W.win, W.gc, 0, 0, W.w, W.ch, 0, W.border+W.ch*y);
	if (T.show_cursor && row_displayed_at(y)==T.c.y) {
		switch (T.cursor_shape) {
		case 0: // filled box
		default:
			// todo: switch to empty box when unfocused
			copy_cursor_part(0, 0, W.cw*cursor_width, W.ch, T.c.x, y);
			break;
		case 1: // underline
			copy_cursor_part(0, W.ch-2, W.cw*cursor_width, 2, T.c.x, y);
			break;
		case 2: // vertical bar
			copy_cursor_part(0, 0, 2, W.ch, T.c.x, y);
			break;
		case 3:; // empty box
			int thick = 1;
			copy_cursor_part(0, 0, W.cw*cursor_width, thick, T.c.x, y);
			copy_cursor_part(0, W.ch-thick, W.cw*cursor_width, thick, T.c.x, y);
			copy_cursor_part(0, thick, thick, W.ch-thick*2, T.c.x, y);
			copy_cursor_part(W.cw*cursor_width-thick, thick, thick, W.ch-thick*2, T.c.x, y);
			break;
		}
		cursor_y = y;
	}
	rows[y].redraw = false;
}

void draw(bool repaint_all) {
	time_log(NULL);
	print("dirty rows: [");
	draw_cursor(T.c.x, T.c.y); // todo: do we need this every time?
	if (cursor_y>=0 && cursor_y<T.height)
		paint_row(cursor_y); // todo: not ideal ehh
	if (repaint_all) {
		// todo: erase the top/bottom borders here?
	}
	for (int y=0; y<T.height; y++) {
		int ry = row_displayed_at(y);
		Row row = get_row(ry);
		if (!row)
			row = blank_row;
		
		bool paint = false;
		if (draw_row(y, row)) {
			paint = true;
			print(row==blank_row ? "~" : "#");
		} else {
			print(".");
		}
		if (repaint_all || paint || T.c.y == y || rows[y].redraw)
			paint_row(y);
	}
	print("] ");
	time_log("redraw");
}

void draw_free(void) {
	// whatever
}

void dirty_cursor(void) {
	
}

// call this when changing palette etc.
void dirty_all(void) {
	for (int y=0; y<T.height; y++)
		rows[y].redraw = true;
}

// todo: display characters CENTERED within the cell rather than aligned to the left side.


