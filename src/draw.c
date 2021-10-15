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

static Row* blank_row = NULL;

// cursor
static XftDraw* cursor_draw = NULL;
static int cursor_width;
static int cursor_y;

// todo: cache the palette colors?
// or perhaps store them as xftcolor internally
static XRenderColor make_color(Color c) {
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
	return (XRenderColor){
		.red = rgb.r*65535/255,
		.green = rgb.g*65535/255,
		.blue = rgb.b*65535/255,
		.alpha = 65535,
	};
}

unsigned long alloc_color(Color c) {
	XRenderColor x = make_color(c);
	return XftColorAllocValue(&x);
}

// these are only used to track the old size in this function
static int drawn_width = -1, drawn_height = -1;
void draw_resize(int width, int height, bool charsize) {
	if (rows) {
		for (int i=0; i<drawn_height; i++) {
			FREE(rows[i].glyphs);
			FREE(rows[i].cells);
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
		rows[y].draw = XftDrawCreate(W.w, W.ch);
		rows[y].redraw = true;
	}
	
	resize_row(&blank_row, T.width);
	for (int x=0; x<T.width; x++)
		blank_row->cells[x] = (Cell){.attrs={.background={.i=-2}}};
	
	// char size changing
	if (charsize) {
		if (cursor_draw)
			XftDrawDestroy(cursor_draw);
		cursor_draw = XftDrawCreate(W.cw*2, W.ch);
	}
}

static int same_color(XRenderColor a, XRenderColor b) {
	return a.red==b.red && a.green==b.green && a.blue==b.blue && a.alpha==b.alpha;
}

// todo: allow drawing multiple at once for efficiency?
static void draw_glyph(XftDraw* draw, Px x, Px y, Glyph g, XRenderColor col, int w) {
	if (!g.font)
		return;
	XftGlyphRender1(PictOpOver, col, g.font, XftDrawPicture(draw), x+(W.cw*w)/2.0, y+W.font_baseline, g.glyph);
}

// todo: make these thicker depending on dpi/fontsize
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
		XRenderColor col = make_color(underline_color);
		XftDrawRect(draw, col, winx, W.font_baseline+1, width*W.cw, underline);
	}
	if (c.attrs.strikethrough) {
		XftDrawRect(draw, make_color(c.attrs.color), winx, W.font_baseline*2/3, width*W.cw, 1);
	}
}

static void draw_cursor(int x, int y) {
	x = limit(x, 0, T.width); // not -1
	y = limit(y, 0, T.height-1);
	
	xim_spot(x, y);
	
	Row* row = T.current->rows[y];
	Cell temp;
	if (row && x<T.width)
		temp = row->cells[x];
	else
		temp = (Cell){0};
	temp.attrs.color = temp.attrs.background;
		
	int width = temp.wide==1 ? 2 : 1;
	
	// draw background
	XftDrawRect(cursor_draw, make_color((Color){.i=-3}), 0, 0, W.cw*width, W.ch);
	
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
		y1 -= T.scroll;
		y2 -= T.scroll;
	}
	y1 = limit(y1, 0, T.height-1);
	y2 = limit(y2, y1, T.height);
	if (y2<=y1)
		return;
	rotate(amount, y2-y1, &rows[y1]);
	for (int y=y1; y<y2; y++)
		rows[y].redraw = true;
}

static bool draw_row(int y, Row* row) {
	// see if row matches what's drawn onscreen
	if (!memcmp(&row->cells, rows[y].cells, sizeof(Cell)*T.width))
		return false;
	memcpy(rows[y].cells, &row->cells, T.width*sizeof(Cell));
	// if blank_row was passed (special case for scrollback out of bounds things)
	if (row==blank_row) {
		XftDrawRect(rows[y].draw, make_color((Color){.truecolor=true,.rgb=T.background}), 0, 0, W.w, W.ch);
		return true;
	}
	
	// draw left border background
	XftDrawRect(rows[y].draw, make_color((Color){.i= row->cont?-3:-2 }), 0, 0, W.border, W.ch);
	// draw cell backgrounds
	XRenderColor prev_color = make_color(row->cells[0].attrs.background);
	int prev_start = 0;
	int x;
	for (x=1; x<T.width; x++) {
		XRenderColor bg = make_color(row->cells[x].attrs.background);
		if (!same_color(bg, prev_color)) {
			XftDrawRect(rows[y].draw, prev_color, W.border+W.cw*prev_start, 0, W.cw*(prev_start-x-1), W.ch);
			prev_start = x;
			prev_color = bg;
		}
	}
	XftDrawRect(rows[y].draw, prev_color, W.border+W.cw*prev_start, 0, W.cw*(prev_start-x-1), W.ch);
	// todo: why does the bg color extend too far in fullscreen?
	// draw right border background
	XftDrawRect(rows[y].draw, make_color((Color){.i = row->wrap?-3:-2}), W.border+W.cw*T.width, 0, W.border, W.ch);
	// 
		//XftDrawRect(rows[y].draw, make_color((Color){.i = -3}), W.border+W.cw*row->length, 0, W.border, W.ch);
	
	// draw text
	// todo: we need to handle combining chars here!!
	// 
	Glyph* specs = rows[y].glyphs;
	cells_to_glyphs(T.width, row->cells, specs, true);
	
	for (int i=0; i<T.width; i++) {
		if (specs[i].font)
			draw_glyph(rows[y].draw, W.border+i*W.cw, 0, specs[i], make_color(row->cells[i].attrs.color), row->cells[i].wide==1 ? 2 : 1);
	}
	
	// draw strikethrough and underlines
	for (int x=0; x<T.width; x++)
		draw_char_overlays(rows[y].draw, W.border+x*W.cw, row->cells[x]);
	
	return true;
}

static int row_displayed_at(int y) {
	if (T.current == &T.buffers[0])
		return y-T.scroll;
	return y;
}

void copy_cursor_part(Px x, Px y, Px w, Px h, int cx, int cy) {
	XftDrawPut(cursor_draw, x, y, w, h, W.border+cx*W.cw+x, W.border+W.ch*cy+y);
}

// todo: vary thickness of cursors and lines based on font size

// todo: keep better track of where cursor is rendered
static void paint_row(int y) {
	XftDrawPut(rows[y].draw, 0, 0, W.w, W.ch, 0, W.border+W.ch*y);
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
		Row* row = get_row(ry);
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


