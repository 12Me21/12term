#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

#define index index_

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

void init_term(Term* t, int width, int height) {
	t->width = width;
	t->height = height;
	t->current = &t->buffers[0];
	t->scrollback.size = 0;
	
	init_palette(t);
	init_scrollback(t);
	
	t->c = (Cursor){
		.x = 0, .y = 0,
		.attrs = {
			.color = (Color){.i = -1},
			.background = (Color){.i = -2},
		},
	};
	t->saved_cursor = t->c;
	
	for (int i=0; i<2; i++) {
		Row* rows = malloc(height*sizeof(Row*));
		t->buffers[i] = (Buffer){
			.rows = rows,
			.scroll_top = 0,
			.scroll_bottom = height,
		};
		for (int y=0; y<height; y++) {
			rows[y] = malloc(width*sizeof(Cell));
			for (int x=0; x<width; x++) {
				rows[y][x] = (Cell){
					.chr = 0,
					.attrs = {
						.color = (Color){.i = -1},
						.background = (Color){.i = -2},
					},
				};
			}
		}
	}
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

void init_row(Term* t, int y) {
	for (int i=0; i<t->width; i++) {
		t->current->rows[y][i] = (Cell){
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
		t->scrollback.rows = realloc(t->scrollback.rows, sizeof(Cell*)*length);
		t->scrollback.size = length;
	}
	// now insert the row
	t->scrollback.rows[t->scrollback.lines++] = t->current->rows[y];
	// remove the row from the buffer itself so it doesn't get freed later
	t->current->rows[y] = NULL;
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
		//if (y1==0) {
		//	push_scrollback(t, y);
		//	t->current->rows[y] = malloc(sizeof(Cell)*t->width);
		//}
		init_row(t, y);
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

void put_char(Term* t, Char c) {
	int width = char_width(c);
	
	if (t->c.x >= t->width) {
		index(t, 1);
		t->c.x = 0;
	}
	
	Cell* dest = &t->current->rows[t->c.y][t->c.x];
	dest->chr = c;
	dest->attrs = t->c.attrs;
	if (width==2)
		dest->attrs.wide = width;
	
	t->c.x += width;
}
