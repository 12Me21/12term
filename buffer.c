#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

#define index index_

#include "common.h"
#include "buffer.h"
#include "debug.h"

const RGBColor default_palette[16] = {
	//r , g , b 
	{  0,  0,  0},
	{170,  0,  0},
	{  0,170,  0},
	{170, 85,  0},
	{  0,  0,170},
	{170,  0,170},
	{  0,170,170},
	{170,170,170},
	{ 85, 85, 85},
	{255, 85, 85},
	{ 85,255, 85},
	{255,255, 85},
	{ 85, 85,255},
	{255, 85,255},
	{ 85,255,255},
	{255,255,255},
};
const RGBColor default_foreground = {  0,  0,  0};
const RGBColor default_background = {255,255,255};

void init_palette(Term* t) {
	t->foreground = default_foreground;
	t->background = default_background;
	// normal 16 indexed colors
	int p = 0;
	for (int i=0; i<16; i++)
		t->palette[p++] = default_palette[i];
	// 6x6x6 rgb cube
	const int brightness[6] = {0, 95, 135, 175, 215, 255};
	for (int i=0; i<6*6*6; i++) {
		t->palette[p++] = (RGBColor){
			brightness[i/6/6 % 6],
			brightness[i/6 % 6],
			brightness[i % 6],
		};
	}
	// fill the rest with grayscale
	for (int i=0; i<256-16-6*6*6; i++) {
		t->palette[p++] = (RGBColor) {
			8 + 10*i, 8 + 10*i, 8 + 10*i,
		};
	}
}

void init_scrollback(Term* t) {
	t->scrollback.size = 0;
	t->scrollback.lines = 0;
}

void clear_row(Term* t, Buffer* buffer, int y, int start) {
	Row row = buffer->rows[y];
	for (int i=start; i<t->width; i++) {
		// todo: check for wide char halves!
		row[i] = (Cell){
			.chr=0,
			.attrs = {
				.color = t->c.attrs.color,
				.background = t->c.attrs.background,
			},
		};
	}
}

static void push_scrollback(Term* t, int y) {
	if (y<0 || y>=t->height)
		return;
	// if scrollback is full, allocate more space
	if (t->scrollback.lines >= t->scrollback.size) {
		int length = t->scrollback.size + 1000;
		REALLOC(t->scrollback.rows, length);
		t->scrollback.size = length;
	}
	// now insert the row
	t->scrollback.rows[t->scrollback.lines++] = t->current->rows[y];
	// remove the row from the buffer itself so it doesn't get freed later
	t->current->rows[y] = NULL;
}

void term_resize(Term* t, int width, int height) {
	if (width != t->width) {
		int old_width = t->width;
		t->width = width;
		// resize existing rows
		// todo: option to re-wrap text?
		for (int i=0; i<2; i++) {
			for (int y=0; y<t->height && y<height; y++) {
				REALLOC(t->buffers[i].rows[y], t->width);
				if (t->width > old_width)
					clear_row(t, &t->buffers[i], y, old_width);
			}
		}
		// update tab stops
		REALLOC(t->tabs, t->width); //todo: init this
	}
	
	// height decrease
	if (height < t->height) {
		int diff = t->height-height;
		// alt buffer: free rows at bottom
		for (int y=height; y<t->height; y++)
			free(t->buffers[1].rows[y]);
		// main buffer: put lines into scrollback and shift the rest up
		for (int y=0; y<diff; y++)
			push_scrollback(t, y);
		for (int y=0; y<height; y++)
			t->buffers[0].rows[y] = t->buffers[0].rows[y+diff];
		// realloc lists of lines
		REALLOC(t->buffers[1].rows, height);
		REALLOC(t->buffers[0].rows, height);
		t->height = height;
	} else if (height > t->height) { // height INCREASE
		// realloc lists of lines
		int old_height = t->height;
		t->height = height;
		REALLOC(t->buffers[1].rows, height);
		REALLOC(t->buffers[0].rows, height);
		// alt buffer: add rows at bottom
		for (int y=old_height; y<t->height; y++) {
			ALLOC(t->buffers[1].rows[y], t->width);
			clear_row(t, &t->buffers[1], y, 0);
		}
		// main buffer: also add rows at bottom
		// todo: option to move lines out of scrollback instead?
		for (int y=old_height; y<t->height; y++) {
			ALLOC(t->buffers[0].rows[y], t->width);
			clear_row(t, &t->buffers[0], y, 0);
		}
	}
	
	// todo: how do we handle the scrolling regions?
	for (int i=0; i<2; i++) {
		t->buffers[i].scroll_top = 0;
		t->buffers[i].scroll_bottom = t->height;
	}
}

void init_term(Term* t, int width, int height) {
	*t = (Term) {
		// REMEMBER: this sets all the other fields to 0
		.current = &t->buffers[0],
		.show_cursor = true,
		.c = {
			.x = 0, .y = 0,
			.attrs = {
				.color = (Color){.i = -1},
				.background = (Color){.i = -2},
			},
		},
	};
	t->saved_cursor = t->c;
	
	init_palette(t);
	init_scrollback(t);
	
	term_resize(t, width, height);
}

static int char_width(Char c) {
	int width;
	if (c<=128) {
		width = 1;
	} else {
		width = wcwidth(c);
		if (width<0)
			width = 1;
	}
	return width;
}

void rotate(int count, int itemsize, unsigned char data[count][itemsize], int amount) {
	while (amount<0)
		amount += count;
	amount %= count;
	int a=0;
	int b=0;
	unsigned char temp[itemsize];
	for (int i=0; i<count; i++) {
		b = (b+amount) % count;
		if (b==a)
			b = ++a;
		if (b!=a) {
			memcpy(temp, data[a], itemsize);
			memcpy(data[a], data[b], itemsize);
			memcpy(data[b], temp, itemsize);
		}
	}
}

void scroll_up(Term* t, int amount) {
	if (!amount)
		return;
	int y1 = t->current->scroll_top;
	int y2 = t->current->scroll_bottom;
	print("scrolling (%d,%d)\n", y1, y2);
	if (y1<0)
		y1 = 0;
	if (y2>t->height)
		y2 = t->height;
	
	for (int y=y1; y<y1+amount; y++) {
		// if we are on the main screen, and the scroll region starts at the top of the screen, we add the lines to the scrollback list.
		if (y1==0 && t->current==&t->buffers[0]) {
			push_scrollback(t, y);
			ALLOC(t->current->rows[y], t->width);
		}
		clear_row(t, t->current, y, 0);
	}
	rotate(y2-y1, sizeof(Cell*), (void*)&t->current->rows[y1], -amount);
}

int cursor_down(Term* t, int amount) {
	if (amount<=0)
		return 0;
	int next = t->c.y + amount;
	int m = t->current->scroll_bottom;
	// cursor started above bottom margin,
	if (t->c.y < m) {
		// and hit the margin
		if (next >= m) {
			t->c.y = m-1;
			return next - (m-1);
		}
	} else //otherwise
		// if cursor hit bottom of screen
		if (next >= t->height)
			next = t->height-1;
	// move cursor
	t->c.y = next;
	return 0;
}

void index(Term* t, int amount) {
	if (amount<=0)
		return;
	// cursor is below scrolling region already, so we just move it down
	if (t->c.y >= t->current->scroll_bottom) {
		cursor_down(t, amount);
	} else { //when the cursor starts out above the scrolling region
		int push = cursor_down(t, amount);
		// check if the cursor tried to pass through the margin
		if (push > 0)
			scroll_up(t, push);
	}
}

static int add_combining_char(Term* t, int x, int y, Char c) {
	Cell* dest = &t->current->rows[y][x];
	if (x<0)
		return 0; //if printing in the first column
	if (dest->wide==-1) {
		if (x==0)
			return 0; //should never happen
		dest--;
		x--;
	}
	for (int i=0; i<15; i++) {
		if (dest->combining[i]==0) {
			dest->combining[i] = c;
			dest->combining[i+1] = 0;
			return 1;
		}
	}
	print("too many combining chars in cell %d,%d!\n", x, y);
	return 1; //we still return 1, because we don't want to put the combining char into the next cell
}

void put_char(Term* t, Char c) {
	int width = char_width(c);
	
	if (width==0) {
		if (add_combining_char(t, t->c.x-1, t->c.y, c))
			return;
		width = 1;
	}
	
	if (t->c.x+width > t->width) {
		index(t, 1);
		t->c.x = 0;
	}
	
	Cell* dest = &t->current->rows[t->c.y][t->c.x];
	// if overwriting the first half of a wide character
	if (dest->wide==1) {
		if (t->c.x+1 < t->width) {
			if (dest[1].wide==-1) {
				dest[1].chr = 0;
				dest[1].wide = 0;
				dest[1].combining[0] = 0;
			}
		}
	} else if (dest->wide==-1) {
		// overwriting the second half of a wide character
		if (t->c.x-1 >= 0) {
			if (dest[-1].wide==1) {
				dest[-1].chr = 0;
				dest[-1].wide = 0;
				dest[-1].combining[0] = 0;
			}
		}
	}
	
	dest->chr = c;
	dest->combining[0] = 0;
	dest->attrs = t->c.attrs;
	// inserting a wide character
	if (width==2) {
		dest->wide = 1;
		if (t->c.x+1 < t->width) { //should always be true
			if (dest[1].wide==1) {
				if (t->c.x+2 < t->width) {
					if (dest[2].wide==-1) {
						dest[2].chr = 0;
						dest[2].wide = 0;
						dest[2].combining[0] = 0;
					}
				}
			}
			dest[1].chr = 0;
			dest[1].combining[0] = 0;
			dest[1].attrs = dest->attrs;
			dest[1].wide = -1;
		}
	}
	
	t->c.x += width;
}

void clear_region(Term* t, int x1, int y1, int x2, int y2) {
	// todo: warn about this
	if (x1<0)
		x1 = 0;
	if (y1<0)
		y1 = 0;
	if (x2>t->width)
		x2 = t->width;
	if (y2>t->height)
		y2 = t->height;
	// todo: handle wide chars
	
	for (int y=y1; y<y2; y++) {
		for (int x=x1; x<x2; x++) {
			t->current->rows[y][x] = (Cell){
				.chr=0,
				.attrs = {
					.color = t->c.attrs.color,
					.background = t->c.attrs.background,
				},
			};
		}
	}
}

void backspace(Term* t) {
	if (t->c.x>0)
		t->c.x--;
}

void resize_screen(Term* t, int width, int height) {
	
}

int cursor_up(Term* t, int amount) {
	if (amount<=0)
		return 0;
	int next = t->c.y - amount;
	int mar = t->current->scroll_top;
	// cursor started below top margin,
	if (t->c.y >= mar) {
		// and hit the margin
		if (next < mar) {
			t->c.y = mar;
			return next - mar;
		}
	} else //otherwise
		// if cursor hit top
		if (next < 0)
			next = 0;
	// move cursor
	t->c.y = next;
	return 0;
}

void cursor_right(Term* t, int amount) {
	if (amount<=0) // should we do the <= check? calling this with amount=0 would potentially move the cursor out of the right margin column.(and should that even happen?)
		return;
	// todo: does this ever wrap?
	t->c.x += amount;
	if (t->c.x >= t->width)
		t->c.x = t->width-1;
}

void cursor_left(Term* t, int amount) {
	if (amount<=0)
		return;
	// todo: does this ever wrap?
	t->c.x -= amount;
	if (t->c.x < 0)
		t->c.x = 0;
}

void cursor_to(Term* t, int x, int y) {
	if (x<0)
		x=0;
	if (y<0)
		y=0;
	if (x>=t->width)
		x=t->width-1;
	if (y>=t->height)
		y=t->height-1;
	t->c.x = x;
	t->c.y = y;
}

void delete_chars(Term* t, int n) {
	if (n<0)
		return;
	if (n > t->width-t->c.x)
		n = t->width-t->c.x;
	Row line = t->current->rows[t->c.y];
	memmove(&line[t->c.x], &line[t->c.x+n], sizeof(Cell)*(t->width-t->c.x-n));
	clear_region(t, t->width-n, t->c.y, t->width, t->c.y+1);
}

void insert_blank(Term* t, int n) {
	if (n<0)
		return;
	if (n > t->width-t->c.x)
		n = t->width-t->c.x;
	
	int dst = t->c.x + n;
	int src = t->c.x;
	int size = t->width - dst;
	Row line = t->current->rows[t->c.y];
	memmove(&line[dst], &line[src], size * sizeof(Cell));
	clear_region(t, src, t->c.y, dst, t->c.y+1);
}


void delete_lines(Term* t, int n) {
	if (t->c.y < t->current->scroll_top)
		return;
	if (t->c.y >= t->current->scroll_bottom)
		return;
	int max = t->current->scroll_bottom - t->current->scroll_top;
	if (n > max)
		n = max;
	if (n < 0)
		return;
	// scroll lines up
	rotate(t->current->scroll_bottom-t->c.y, sizeof(Cell*), (void*)&t->current->rows[t->c.y], -n);
	// clear the rows at the bottom
	for (int i=0; i<n; i++)
		clear_row(t, t->current, t->current->scroll_bottom-i-1, 0);
}
