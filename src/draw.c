// Drawing graphics

#include <string.h>

#include <X11/Xlib.h>
#include "xft/Xft.h"

#include "common.h"
#include "x.h"
#include "buffer.h"
#include "draw.h"
#include "draw2.h"
#include "event.h"

#define Glyph Glyph_
typedef struct Glyph {
	struct GlyphData* glyph; //null if glyph is empty
	// keys for caching
	Char chr;
	char style; // whether bold/italic etc.
	// when turning cells into glyphs, if the prev 2 values match the new cell's, the cached glyph is used
	// todo: we need to store which cell the glyph is in, so we can handle combining chars
	int x;
} Glyph;

typedef struct XftDraw {
	Drawable drawable;
	Picture pict;
} XftDraw;

typedef struct DrawRow {
	// cache of the glyphs and cells
	Cell* cells;
	Glyph* glyphs;
	// framebuffer
	XftDraw draw;
	// to force a redraw 
	bool redraw;
} DrawRow;

static DrawRow* rows = NULL;

static Row* blank_row = NULL;

// cursor
static XftDraw cursor_draw = {0, 0};
static int cursor_width; // in cells
static int cursor_y; // cells

//Drawable frame_buffer = None;
//GC fb_gc = None;

// convert a Color (indexed or rgb) into XRenderColor (rgb)
XRenderColor make_color(Color c) {
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

static void draw_rect(XftDraw draw, Color color, Px x, Px y, Px width, Px height) {
	XRenderColor c = make_color(color);
	XRenderFillRectangle(W.d, PictOpSrc, draw.pict, &c, x, y, width, height);
}

static XftDraw draw_create(Px w, Px h) {
	Drawable d = XCreatePixmap(W.d, W.win, w, h, DefaultDepth(W.d, W.scr));
	return (XftDraw){
		.drawable = d,
		.pict = XRenderCreatePicture(W.d, d, W.format, 0, NULL),
	};
}

static void draw_destroy(XftDraw draw) {
	XFreePixmap(W.d, draw.drawable);
	XRenderFreePicture(W.d, draw.pict);
}
// todo: add _replace back? this only gets used on resize so is it worth it, idk?

static int cell_fontstyle(const Cell* c) {
	return (c->attrs.weight==1) | (c->attrs.italic)<<1;
}

static void cells_to_glyphs(int len, Cell cells[len], Glyph glyphs[len], bool cache) {
	FOR (i, len) {
		Char chr = cells[i].chr;
		// skip blank cells
		if (cells[i].wide==-1 || chr==0 || chr==' ') {
			glyphs[i].chr = chr;
			glyphs[i].glyph = NULL;
			continue;
		}
		int style = cell_fontstyle(&cells[i]);
		if (!cache || glyphs[i].chr!=chr || glyphs[i].style!=style)
			glyphs[i].glyph = cache_lookup(chr, style);
	}
}

// these are only used to track the old size in this function
static int drawn_width = -1, drawn_height = -1;
void draw_resize(int width, int height, bool charsize) {
	/* if (frame_buffer) */
	/* 	XFreePixmap(W.d, frame_buffer); */
	/* frame_buffer = XCreatePixmap(W.d, W.win, W.w, W.h, DefaultDepth(W.d, W.scr)); */
	/* if (fb_gc) */
	/* 	XFreeGC(W.d, fb_gc); */
	/* fb_gc = XCreateGC(W.d, frame_buffer, GCGraphicsExposures, &(XGCValues){ */
	/* 		.graphics_exposures = False, */
	/* 	}); */
	
	if (rows) {
		FOR (i, drawn_height) {
			FREE(rows[i].glyphs);
			FREE(rows[i].cells);
			draw_destroy(rows[i].draw);
		}
	}
	drawn_height = height;
	drawn_width = width;
	REALLOC(rows, height);
	FOR (y, T.height) {
		ALLOC(rows[y].glyphs, T.width);
		ALLOC(rows[y].cells, T.width);
		FOR (x, T.width) {
			rows[y].glyphs[x] = (Glyph){0}; // mreh
			rows[y].cells[x] = (Cell){0}; //ehnnnn
		}
		rows[y].draw = draw_create(W.w, W.ch);
		rows[y].redraw = true;
	}
	
	resize_row(&blank_row, T.width, 0); // 0 should be old width but whatever
	FOR (x, T.width) {
		blank_row->cells[x] = (Cell){.attrs={.background={.i=-2}}};
	}
	
	// char size changing
	if (charsize) {
		if (cursor_draw.drawable)
			draw_destroy(cursor_draw);
		cursor_draw = draw_create(W.cw*2, W.ch);
	}
}

static int same_color(Color ca, Color cb) {
	XRenderColor a = make_color(ca), b = make_color(cb);
	return a.red==b.red && a.green==b.green && a.blue==b.blue && a.alpha==b.alpha;
}

// todo: allow drawing multiple at once for efficiency?
static void draw_glyph(XftDraw draw, Px x, Px y, Glyph g, Color col, int w) {
	if (!g.glyph)
		return;
	render_glyph(make_color(col), draw.pict, x+(W.cw*w)/2.0, y+W.font_baseline, g.glyph);
}

// todo: make these thicker depending on dpi/fontsize
static void draw_char_overlays(XftDraw draw, Px winx, Cell c) {
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
		draw_rect(draw, underline_color, winx, W.font_baseline+1, width*W.cw, underline);
	}
	if (c.attrs.strikethrough) {
		draw_rect(draw, c.attrs.color, winx, W.font_baseline*2/3, width*W.cw, 1);
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
	draw_rect(cursor_draw, (Color){.i=-3}, 0, 0, W.cw*width, W.ch);
	
	// draw char
	if (temp.chr) {
		Glyph spec[1];
		cells_to_glyphs(1, &temp, spec, false);
		draw_glyph(cursor_draw, 0, 0, spec[0], temp.attrs.color, width);
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
	
	FOR (i, length) {
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
	return;
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
	// todo: we don't store the wrap flags in here.
	// so if you're debugging and want them visible, you must remove this line too
	// todo: i think this is not working reliably?
	if (!memcmp(&row->cells, rows[y].cells, sizeof(Cell)*T.width))
		return false;
	memcpy(rows[y].cells, &row->cells, T.width*sizeof(Cell));
	// if blank_row was passed (special case for scrollback out of bounds things)
	if (row==blank_row) {
		draw_rect(rows[y].draw, (Color){.truecolor=true,.rgb=T.background}, 0, 0, W.w, W.ch);
		return true;
	}
	
	// draw left border background
	draw_rect(rows[y].draw, (Color){.i= /*row->cont?-3:*/-2}, 0, 0, W.border, W.ch);
	// draw cell backgrounds
	Color prev_color = row->cells[0].attrs.background;
	int prev_start = 0;
	int x;
	for (x=1; x<T.width; x++) {
		Color bg = row->cells[x].attrs.background;
		if (!same_color(bg, prev_color)) {
			draw_rect(rows[y].draw, prev_color, W.border+W.cw*prev_start, 0, W.cw*(prev_start-x-1), W.ch);
			prev_start = x;
			prev_color = bg;
		}
	}
	draw_rect(rows[y].draw, prev_color, W.border+W.cw*prev_start, 0, W.cw*(prev_start-x-1), W.ch);
	// todo: why does the bg color extend too far in fullscreen?
	// draw right border background
	draw_rect(rows[y].draw, (Color){.i = /*row->wrap?-3:*/-2}, W.border+W.cw*T.width, 0, W.border, W.ch);
	//draw_rect(rows[y].draw, (Color){.i = -3}, W.border+W.cw*row->length, 0, W.border, W.ch);
	
	// draw text
	// todo: we need to handle combining chars here!!
	Glyph* specs = rows[y].glyphs;
	cells_to_glyphs(T.width, row->cells, specs, true);
	
	FOR (i, T.width) {
		if (specs[i].glyph)
			draw_glyph(rows[y].draw, W.border+i*W.cw, 0, specs[i], row->cells[i].attrs.color, row->cells[i].wide==1 ? 2 : 1);
	}
	
	// draw strikethrough and underlines
	FOR (x, T.width) {
		draw_char_overlays(rows[y].draw, W.border+x*W.cw, row->cells[x]);
	}
	
	return true;
}

static int row_displayed_at(int y) {
	if (T.current == &T.buffers[0])
		return y-T.scroll;
	return y;
}

static void draw_put(XftDraw draw, Px x, Px y, Px w, Px h, Px dx, Px dy) {
	XCopyArea(W.d, draw.drawable, W.win, W.gc, x, y, w, h, dx, dy);
}
static void copy_cursor_part(Px x, Px y, Px w, Px h, int cx, int cy) {
	draw_put(cursor_draw, x, y, w, h, W.border+cx*W.cw+x, W.border+W.ch*cy+y);
}
//void composite(void) {
  //	XCopyArea(W.d, frame_buffer, W.win, W.gc, 0, 0, W.w, W.h, 0, 0);
//}

// todo: vary thickness of cursors and lines based on font size

// todo: keep better track of where cursor is rendered
static void paint_row(int y) {
	draw_put(rows[y].draw, 0, 0, W.w, W.ch, 0, W.border+W.ch*y);
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
	// wait how does this work with scrolling?
	// and yeah we don't need this every time
	draw_cursor(T.c.x, T.c.y); // todo: do we need this every time?
	if (cursor_y>=0 && cursor_y<T.height)
		paint_row(cursor_y); // todo: not ideal ehh
	if (repaint_all) {
		// todo: erase the top/bottom borders here?
	}
	FOR (y, T.height) {
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
	//composite();
	//time_log("comp");
}

void draw_free(void) {
	// whatever
}

void dirty_cursor(void) {
	
}

// call this when changing palette etc.
void dirty_all(void) {
	FOR (y, T.height) {
		rows[y].redraw = true;
	}
}
