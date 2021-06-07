#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

#define index index_

#include "common.h"
#include "buffer.h"
#include "buffer2.h"
#include "debug.h"
#include "render.h"

Term T; // ok there's really no reason to ever need more than one of these anyway.

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
const RGBColor default_cursor = {  0,192,  0};
const RGBColor default_foreground = {  0,  0,  0};
const RGBColor default_background = {255,255,255};

void init_palette(void) {
	T.foreground = default_foreground;
	T.background = default_background;
	T.cursor_background = default_cursor;
	// normal 16 indexed colors
	int p = 0;
	for (int i=0; i<16; i++)
		T.palette[p++] = default_palette[i];
	// 6x6x6 rgb cube
	const int brightness[6] = {0, 95, 135, 175, 215, 255};
	for (int i=0; i<6*6*6; i++) {
		T.palette[p++] = (RGBColor){
			brightness[i/6/6 % 6],
			brightness[i/6 % 6],
			brightness[i % 6],
		};
	}
	// fill the rest with grayscale
	for (int i=0; i<256-16-6*6*6; i++) {
		T.palette[p++] = (RGBColor) {
			8 + 10*i, 8 + 10*i, 8 + 10*i,
		};
	}
}

void init_scrollback(void) {
	T.scrollback.size = 0;
	T.scrollback.lines = 0;
}

void dirty_all(void) {
	for (int y=0; y<T.height; y++) {
		T.dirty_rows[y] = true;
	}
}

void clear_row(Buffer* buffer, int y, int start) {
	Row row = buffer->rows[y];
	T.dirty_rows[y] = true;
	for (int i=start; i<T.width; i++) {
		// todo: check for wide char halves!
		row[i] = (Cell){
			.chr=0,
			.attrs = {
				.color = T.c.attrs.color,
				.background = T.c.attrs.background,
			},
		};
	}
}

static void push_scrollback(int y) {
	if (y<0 || y>=T.height)
		return;
	// if scrollback is full, allocate more space
	if (T.scrollback.lines >= T.scrollback.size) {
		int length = T.scrollback.size + 1000;
		REALLOC(T.scrollback.rows, length);
		T.scrollback.size = length;
	}
	// now insert the row
	T.scrollback.rows[T.scrollback.lines++] = T.buffers[0].rows[y];
	// remove the row from the buffer itself so it doesn't get freed later
	T.buffers[0].rows[y] = NULL;
}

void term_resize(int width, int height) {
	if (width != T.width) {
		int old_width = T.width;
		T.width = width;
		// resize existing rows
		// todo: option to re-wrap text?
		for (int i=0; i<2; i++) {
			for (int y=0; y<T.height && y<height; y++) {
				REALLOC(T.buffers[i].rows[y], T.width);
				if (T.width > old_width)
					clear_row(&T.buffers[i], y, old_width);
			}
		}
		// update tab stops
		REALLOC(T.tabs, T.width); //todo: init this
		for (int x=0; x<T.width; x++) {
			T.tabs[x] = (x%8 == 0);
		}
	}
	
	// height decrease
	if (height < T.height) {
		int diff = T.height-height;
		// alt buffer: free rows at bottom
		for (int y=height; y<T.height; y++)
			free(T.buffers[1].rows[y]);
		// main buffer: put lines into scrollback and shift the rest up
		for (int y=0; y<diff; y++)
			push_scrollback(y);
		for (int y=0; y<height; y++)
			T.buffers[0].rows[y] = T.buffers[0].rows[y+diff];
		// realloc lists of lines
		T.height = height;
		REALLOC(T.buffers[1].rows, height);
		REALLOC(T.buffers[0].rows, height);
		REALLOC(T.dirty_rows, height);
	} else if (height > T.height) { // height INCREASE
		// realloc lists of lines
		int old_height = T.height;
		REALLOC(T.buffers[1].rows, height);
		REALLOC(T.buffers[0].rows, height);
		REALLOC(T.dirty_rows, height);
		T.height = height;
		// alt buffer: add rows at bottom
		for (int y=old_height; y<T.height; y++) {
			ALLOC(T.buffers[1].rows[y], T.width);
			clear_row(&T.buffers[1], y, 0);
		}
		// main buffer: also add rows at bottom
		// todo: option to move lines out of scrollback instead?
		for (int y=old_height; y<T.height; y++) {
			ALLOC(T.buffers[0].rows[y], T.width);
			clear_row(&T.buffers[0], y, 0);
		}
	}
	
	dirty_all(); // whatever
	// todo: how do we handle the scrolling regions?
	for (int i=0; i<2; i++) {
		T.buffers[i].scroll_top = 0;
		T.buffers[i].scroll_bottom = T.height;
	}
}

void init_term(int width, int height) {
	T = (Term) {
		// REMEMBER: this sets all the other fields to 0
		.current = &T.buffers[0],
		.show_cursor = true,
		.c = {
			.x = 0, .y = 0,
			.attrs = {
				.color = (Color){.i = -1},
				.background = (Color){.i = -2},
			},
		},
	};
	T.saved_cursor = T.c;
	
	init_palette();
	init_scrollback();
	
	term_resize(width, height);
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

void save_cursor(void) {
	T.saved_cursor = T.c;
}

void restore_cursor(void) {
	T.c = T.saved_cursor;
	cursor_to(T.c.x, T.c.y);
}

static void scroll_down(int amount) {
	if (amount<=0)
		return;
	int y1 = T.current->scroll_top;
	int y2 = T.current->scroll_bottom;
	if (y1<0)
		y1 = 0;
	if (y2>T.height)
		y2 = T.height;
	
	rotate(y2-y1, sizeof(Cell*), (void*)&T.current->rows[y1], amount);
	for (int y=y1; y<y2; y++)
		T.dirty_rows[y] = true;
	
	for (int y=y1; y<y2; y++)
		clear_row(T.current, y, 0);
}

static void scroll_up(int amount) {
	if (amount<=0)
		return;
	int y1 = T.current->scroll_top;
	int y2 = T.current->scroll_bottom;
	if (y1<0)
		y1 = 0;
	if (y2>T.height)
		y2 = T.height;
	
	for (int y=y1; y<y1+amount; y++) {
		// if we are on the main screen, and the scroll region starts at the top of the screen, we add the lines to the scrollback list.
		if (y1==0 && T.current==&T.buffers[0]) {
			push_scrollback(y);
			ALLOC(T.current->rows[y], T.width);
		}
	}
	rotate(y2-y1, sizeof(Cell*), (void*)&T.current->rows[y1], -amount);
	//shift_lines(y1+amount, y1, y2-y1-amount);
	for (int y=y1; y<y2; y++)
		T.dirty_rows[y] = true;
	for (int y=y2-amount; y<y2; y++)
		clear_row(T.current, y, 0);
}

void reverse_index(int amount) {
	if (amount<=0)
		return;
	if (T.c.y < T.current->scroll_top)
		cursor_up(amount);
	else {
		int push = cursor_up(amount);
		if (push>0)
			scroll_down(push);
	}
}

int cursor_down(int amount) {
	if (amount<=0)
		return 0;
	int next = T.c.y + amount;
	int m = T.current->scroll_bottom;
	// cursor started above bottom margin,
	if (T.c.y < m) {
		// and hit the margin
		if (next >= m) {
			T.c.y = m-1;
			return next - (m-1);
		}
	} else //otherwise
		// if cursor hit bottom of screen
		if (next >= T.height)
			next = T.height-1;
	// move cursor
	T.c.y = next;
	return 0;
}

void index(int amount) {
	if (amount<=0)
		return;
	// cursor is below scrolling region already, so we just move it down
	if (T.c.y >= T.current->scroll_bottom) {
		cursor_down(amount);
	} else { //when the cursor starts out above the scrolling region
		int push = cursor_down(amount);
		// check if the cursor tried to pass through the margin
		if (push > 0)
			scroll_up(push);
	}
}

static int add_combining_char(int x, int y, Char c) {
	Cell* dest = &T.current->rows[y][x];
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
			T.dirty_rows[y] = true;
			return 1;
		}
	}
	print("too many combining chars in cell %d,%d!\n", x, y);
	return 1; //we still return 1, because we don't want to put the combining char into the next cell
}

void put_char(Char c) {
	int width = char_width(c);
	
	if (width==0) {
		if (add_combining_char(T.c.x-1, T.c.y, c))
			return;
		width = 1;
	}
	
	if (T.c.x+width > T.width) {
		index(1);
		T.c.x = 0;
	}
	
	Cell* dest = &T.current->rows[T.c.y][T.c.x];
	// if overwriting the first half of a wide character
	if (dest->wide==1) {
		if (T.c.x+1 < T.width) {
			if (dest[1].wide==-1) {
				dest[1].chr = 0;
				dest[1].wide = 0;
				dest[1].combining[0] = 0;
			}
		}
	} else if (dest->wide==-1) {
		// overwriting the second half of a wide character
		if (T.c.x-1 >= 0) {
			if (dest[-1].wide==1) {
				dest[-1].chr = 0;
				dest[-1].wide = 0;
				dest[-1].combining[0] = 0;
			}
		}
	}
	
	dest->chr = c;
	dest->combining[0] = 0;
	dest->attrs = T.c.attrs;
	// inserting a wide character
	if (width==2) {
		dest->wide = 1;
		if (T.c.x+1 < T.width) { //should always be true
			if (dest[1].wide==1) {
				if (T.c.x+2 < T.width) {
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
	
	T.c.x += width;
	
	T.dirty_rows[T.c.y] = true;
}

void clear_region(int x1, int y1, int x2, int y2) {
	//print("clearing region (%d %d) (%d %d) with term size %d x %d\n", x1,y1,x2,y2,T.width,T.height);
	// todo: warn about this
	if (x1<0)
		x1 = 0;
	if (y1<0)
		y1 = 0;
	if (x2>T.width)
		x2 = T.width;
	if (y2>T.height)
		y2 = T.height;
	// todo: handle wide chars
	
	for (int y=y1; y<y2; y++) {
		T.dirty_rows[y] = true;
		//print("clearing row %d: %p\n", y, T.current->rows[y]);
		for (int x=x1; x<x2; x++) {
			T.current->rows[y][x] = (Cell){
				.chr=0,
				.attrs = {
					.color = T.c.attrs.color,
					.background = T.c.attrs.background,
				},
			};
		}
	}
}

void backspace(void) {
	if (T.c.x>0)
		T.c.x--;
}

int cursor_up(int amount) {
	if (amount<=0)
		return 0;
	int next = T.c.y - amount;
	int mar = T.current->scroll_top;
	// cursor started below top margin,
	if (T.c.y >= mar) {
		// and hit the margin
		if (next < mar) {
			T.c.y = mar;
			return next - mar;
		}
	} else //otherwise
		// if cursor hit top
		if (next < 0)
			next = 0;
	// move cursor
	T.c.y = next;
	return 0;
}

void cursor_right(int amount) {
	if (amount<=0) // should we do the <= check? calling this with amount=0 would potentially move the cursor out of the right margin column.(and should that even happen?)
		return;
	// todo: does this ever wrap?
	T.c.x += amount;
	if (T.c.x >= T.width)
		T.c.x = T.width-1;
}

void cursor_left(int amount) {
	if (amount<=0)
		return;
	// todo: does this ever wrap?
	T.c.x -= amount;
	if (T.c.x < 0)
		T.c.x = 0;
}

void cursor_to(int x, int y) {
	if (x<0)
		x=0;
	if (y<0)
		y=0;
	if (x>=T.width)
		x=T.width-1;
	if (y>=T.height)
		y=T.height-1;
	T.c.x = x;
	T.c.y = y;
}

void delete_chars(int n) {
	if (n<0)
		return;
	if (n > T.width-T.c.x)
		n = T.width-T.c.x;
	Row line = T.current->rows[T.c.y];
	memmove(&line[T.c.x], &line[T.c.x+n], sizeof(Cell)*(T.width-T.c.x-n));
	clear_region(T.width-n, T.c.y, T.width, T.c.y+1);
}

void insert_blank(int n) {
	if (n<0)
		return;
	if (n > T.width-T.c.x)
		n = T.width-T.c.x;
	
	int dst = T.c.x + n;
	int src = T.c.x;
	int size = T.width - dst;
	Row line = T.current->rows[T.c.y];
	memmove(&line[dst], &line[src], size * sizeof(Cell));
	clear_region(src, T.c.y, dst, T.c.y+1);
}

void insert_lines(int n) {
	if (T.c.y < T.current->scroll_top)
		return;
	if (T.c.y >= T.current->scroll_bottom)
		return;
	int max = T.current->scroll_bottom - T.c.y;
	if (n > max)
		n = max;
	if (n <= 0)
		return;
	// scroll lines down
	rotate(T.current->scroll_bottom-T.c.y, sizeof(Cell*), (void*)&T.current->rows[T.c.y], n);
	// clear rows at top
	for (int i=0; i<n; i++)
		clear_row(T.current, T.c.y+i, 0);
	
	for (int y=T.c.y; y<T.current->scroll_bottom; y++)
		T.dirty_rows[y] = true;
}
	
void delete_lines(int n) {
	if (T.c.y < T.current->scroll_top)
		return;
	if (T.c.y >= T.current->scroll_bottom)
		return;
	int max = T.current->scroll_bottom - T.c.y;
	if (n > max)
		n = max;
	if (n <= 0)
		return;
	// scroll lines up
	rotate(T.current->scroll_bottom-T.c.y, sizeof(Cell*), (void*)&T.current->rows[T.c.y], -n);
	// clear the rows at the bottom
	for (int i=0; i<n; i++)
		clear_row(T.current, T.current->scroll_bottom-i-1, 0);
	
	for (int y=T.c.y; y<T.current->scroll_bottom; y++)
		T.dirty_rows[y] = true;
}

void back_tab(int n) {
	while (T.c.x > 0 && n > 0) {
		T.c.x--;
		if (T.tabs[T.c.x])
			n--;
	}
}

void forward_tab(int n) {
	while (T.c.x < T.width-1 && n > 0) {
		T.c.x++;
		if (T.tabs[T.c.x])
			n--;
	}
}

void erase_characters(int n) {
	if (n > T.width-T.c.x)
		n = T.width-T.c.x;
	clear_region(T.c.x, T.c.y, T.c.x+n, T.c.y+1);
}
