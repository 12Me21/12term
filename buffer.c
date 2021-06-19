// functions for controlling the text in the screen buffer

#define _XOPEN_SOURCE 600
#include <wchar.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"
#include "buffer.h"
#include "ctlseqs.h"

Term T; // ok there's really no reason to ever need more than one of these anyway.

RGBColor default_palette[16] = {
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
RGBColor default_cursor = {  0,192,  0};
RGBColor default_foreground = {  0,  0,  0};
RGBColor default_background = {255,255,255};
int default_cursor_style = 2;

static void limit(int* x, int min, int max) {
	if (*x<min)
		*x = min;
	else if (*x>max)
		*x = max;
}

static void init_palette(void) {
	T.foreground = default_foreground;
	T.background = default_background;
	T.cursor_color = default_cursor;
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

void term_free(void) {
	for (int i=0; i<2; i++) {
		for (int y=0; y<T.height; y++)
			free(T.buffers[i].rows[y]);
	}
	free(T.tabs);
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

void dirty_all(void) {
	for (int y=0; y<T.height; y++)
		T.dirty_rows[y] = true;
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
		REALLOC(T.tabs, T.width+1);
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
	T.scroll_top = 0;
	T.scroll_bottom = T.height;
}

// todo: actually use this
void set_cursor_style(int n) {
	if (n==0)
		n = default_cursor_style;
	
	if (n>0 && n<=6) {
		T.cursor_shape = (n-1)/2;
		T.cursor_blink = (n-1)%2==0;
	}
}

void clear_region(int x1, int y1, int x2, int y2) {
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

// todo: confirm which things are reset by this
void full_reset(void) {
	for (int i=0; i<2; i++) {
		T.current = &T.buffers[i];
		clear_region(0, 0, T.width, T.height);
	}
	T.scroll_top = 0;
	T.scroll_bottom = T.height;
	T.current = &T.buffers[0];
	
	T.c = (Cursor){
		.x = 0, .y = 0,
		.attrs = {
			.color = {.i = -1},
			.background = {.i = -2},
		},
	};
	T.saved_cursor = T.c;
	T.show_cursor = true;
	set_cursor_style(0);
	
	init_palette();
	
	for (int i=0; i<T.width+1; i++)
		T.tabs[i] = i%8==0;
	
	T.charsets[0] = 0;
	
	T.bracketed_paste = false;
	T.app_keypad = false;
	T.app_cursor = false;
	
	dirty_all();
	
	reset_parser();
}

void init_term(int width, int height) {
	T = (Term){
		// REMEMBER: this sets all the other fields to 0
		.current = &T.buffers[0],
		.c = {
			.attrs = {
				.color = {.i = -1},
				.background = {.i = -2},
			},
		},
	};
	term_resize(width, height);
	full_reset();
	init_scrollback();
	int f = open("init", O_RDONLY);
	print("init\n");
	if (f) {
		char buf[1024*100];
		while (1) {
			ssize_t len = read(f, buf, sizeof(buf));
			if (len<=0)
				break;
			process_chars(len, buf);
		}
		close(f);
	}
}

// generic array rotate function
static void memswap(int size, unsigned char a[size], unsigned char b[size]) {
	unsigned char temp[size];
	memcpy(temp, a, size);
	memcpy(a, b, size);
	memcpy(b, temp, size);
}
static void rotate(int count, int itemsize, unsigned char data[count][itemsize], int amount) {
	while (amount<0)
		amount += count;
	amount %= count;
	int a=0;
	int b=0;
	
	for (int i=0; i<count; i++) {
		b = (b+amount) % count;
		if (b==a)
			b = ++a;
		if (b!=a)
			memswap(itemsize, data[a], data[b]);
	}
}

// shift the rows in [`y1`,`y2`) by `amount` (negative = up, positive = down)
// and clear the "new" lines
static void shift_rows(int y1, int y2, int amount) {
	rotate(y2-y1, sizeof(Cell*), (void*)&T.current->rows[y1], amount);
	for (int y=y1; y<y2; y++)
		T.dirty_rows[y] = true;
	if (amount>0) {
		for (int y=y1; y<y1+amount; y++)
			clear_row(T.current, y, 0);
	} else {
		for (int y=y2+amount; y<y2; y++)
			clear_row(T.current, y, 0);
	}
}

// move text downwards
static void scroll_down_internal(int amount) {
	int y1 = T.scroll_top;
	int y2 = T.scroll_bottom;
	limit(&amount, 0, y2-y1);
	shift_rows(y1, y2, amount);
}

static void scroll_up_internal(int amount) {
	int y1 = T.scroll_top;
	int y2 = T.scroll_bottom;
	limit(&amount, 0, y2-y1);
	if (y1==0 && T.current==&T.buffers[0])
		for (int y=y1; y<y1+amount; y++) {
		// if we are on the main screen, and the scroll region starts at the top of the screen, we add the lines to the scrollback list.
			push_scrollback(y);
			ALLOC(T.current->rows[y], T.width);
		}
	shift_rows(y1, y2, -amount);
}

void cursor_to(int x, int y) {
	// but is it ok to move the cursor to the offscreen column?
	limit(&x, 0, T.width-1);
	limit(&y, 0, T.height-1);
	T.c.x = x;
	T.c.y = y;
}

// these scroll + move the cursor with the scrolled text
// todo: confirm the cases where these are supposed to move the cursor
void scroll_up(int amount) {
	scroll_up_internal(amount);
	if (T.c.y>=T.scroll_top && T.c.y<T.scroll_bottom) {
		limit(&amount, 0, T.c.y-T.scroll_top);
		cursor_to(T.c.x, T.c.y-amount);
	}
}

void scroll_down(int amount) {
	scroll_down_internal(amount);
	if (T.c.y>=T.scroll_top && T.c.y<T.scroll_bottom) {
		limit(&amount, 0, T.scroll_bottom-1-T.c.y);
		cursor_to(T.c.x, T.c.y+amount);
	}
}

int cursor_up(int amount) {
	if (amount<=0)
		return 0;
	int next = T.c.y - amount;
	int mar = T.scroll_top;
	// cursor started below top margin,
	if (T.c.y >= mar) {
		// and hit the margin
		if (next < mar) {
			T.c.y = mar;
			return mar - next;
		}
	} else //otherwise
		// if cursor hit top
		if (next < 0)
			next = 0;
	// move cursor
	T.c.y = next;
	return 0;
}

void reverse_index(int amount) {
	if (amount<=0)
		return;
	if (T.c.y < T.scroll_top) {
		cursor_up(amount);
	} else {
		int push = cursor_up(amount);
		if (push>0)
			scroll_down_internal(push);
	}
}

// are we sure this can't overflow T.c.y?
int cursor_down(int amount) {
	if (amount<=0)
		return 0;
	int next = T.c.y + amount;
	int m = T.scroll_bottom;
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
	for (int i=0; i<LEN(dest->combining); i++) {
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

// utf-8 decoding macro lol
//#define U(str) sizeof(str)==2 ? str[0] : sizeof(str)==3 ? (int)(str[0]&31)<<6 | (int)(str[1]&63) : sizeof(str)==4 ? (int)(str[0]&15)<<6*2 | (int)(str[1]&63)<<6 | (int)(str[2]&63) : sizeof(str)==5 ? (int)(str[0]&8)<<6*3 | (int)(str[1]&63)<<6*2 | (int)(str[2]&63)<<6 | (int)(str[3]&63) : 0

// todo: many fonts have rather deformed box drawing chars
// (at least, the one I use does lol)
// maybe have an option like xterm's to override them
static const Char DEC_GRAPHICS_CHARSET[128] = {
	['A'] = L'↑', L'↓', L'→', L'←', L'█', L'▚', L'☃',
	['_'] = L' ',
	['`'] = L'◆', L'▒', L'␉', L'␌', L'␍', L'␊', L'°', L'±', L'␤', L'␋', L'┘', L'┐', L'┌', L'└', L'┼', L'⎺', L'⎻', L'─', L'⎼', L'⎽', L'├', L'┤', L'┴', L'┬', L'│', L'≤', L'≥', L'π', L'≠', L'£', L'·',
};

static int char_width(Char c) {
	int width;
	if (c<=128) { // assume ascii chars are never wide, to avoid calling wcwidth all the time
		width = 1;
	} else {
		width = wcwidth(c);
		if (width<0)
			width = 1;
	}
	return width;
}

static void erase_wc_left(int x, int y) {
	if (x+1 < T.width) {
		Cell* dest = &T.current->rows[y][x+1];
		if (dest->wide==-1) {
			*dest = (Cell){
				.attrs = dest->attrs,
				// rest are 0
			};
		}
	}
}

static void erase_wc_right(int x, int y) {
	if (x-1 >= 0) {
		Cell* dest = &T.current->rows[y][x-1];
		if (dest->wide==1) {
			*dest = (Cell){
				.attrs = dest->attrs,
				// rest are 0
			};
		}
	}
}

void forward_index(int amount) {
	if (amount<=0)
		return;
	// cursor is below scrolling region already, so we just move it down
	if (T.c.y >= T.scroll_bottom) {
		cursor_down(amount);
	} else { //when the cursor starts out above the scrolling region
		int push = cursor_down(amount);
		// check if the cursor tried to pass through the margin
		if (push > 0)
			scroll_up_internal(push);
	}
}

void put_char(Char c) {
	if (T.charsets[0] == '0') {
		if (c<128 && c>=0 && DEC_GRAPHICS_CHARSET[c])
			c = DEC_GRAPHICS_CHARSET[c];
	}
	
	int width = char_width(c);
	
	if (width==0) {
		if (add_combining_char(T.c.x-1, T.c.y, c))
			return;
		width = 1;
	}
	
	if (T.c.x+width > T.width) {
		forward_index(1);
		T.c.x = 0;
	}
	
	Cell* dest = &T.current->rows[T.c.y][T.c.x];
	if (dest->wide==1)
		erase_wc_left(T.c.x, T.c.y);
	else if (dest->wide==-1)
		erase_wc_right(T.c.x, T.c.y);
	
	dest->chr = c;
	dest->combining[0] = 0;
	dest->attrs = T.c.attrs;
	if (T.c.attrs.reverse) {
		dest->attrs.color = T.c.attrs.background;
		dest->attrs.background = T.c.attrs.color;
	}
	// inserting a wide character
	if (width==2) {
		dest->wide = 1;
		if (T.c.x+1 < T.width) { //should always be true
			if (dest[1].wide==1)
				erase_wc_left(T.c.x+2, T.c.y);
			// (don't need erase_wc_right because we know the char to the left is new)
			// insert dummy char
			dest[1].chr = 0;
			dest[1].combining[0] = 0;
			dest[1].attrs = dest->attrs;
			dest[1].wide = -1;
		}
	} else
		dest->wide = 0;
	
	T.c.x += width;
	
	T.dirty_rows[T.c.y] = true;
}

void backspace(void) {
	if (T.c.x>0)
		T.c.x--;
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

void delete_chars(int n) {
	limit(&n, 0, T.width-T.c.x);
	if (!n)
		return;
	Row line = T.current->rows[T.c.y];
	memmove(&line[T.c.x], &line[T.c.x+n], sizeof(Cell)*(T.width-T.c.x-n));
	clear_row(T.current, T.c.y, T.width-n);
}

void insert_blank(int n) {
	limit(&n, 0, T.width-T.c.x);
	if (!n)
		return;
	
	int dst = T.c.x + n;
	int src = T.c.x;
	int size = T.width - dst;
	Row line = T.current->rows[T.c.y];
	memmove(&line[dst], &line[src], size * sizeof(Cell));
	clear_region(src, T.c.y, dst, T.c.y+1);
}

void insert_lines(int n) {
	if (T.c.y < T.scroll_top)
		return;
	if (T.c.y >= T.scroll_bottom)
		return;
	limit(&n, 0, T.scroll_bottom - T.c.y);
	if (!n)
		return;
	// scroll lines down
	shift_rows(T.c.y, T.scroll_bottom, n);
}
	
void delete_lines(int n) {
	if (T.c.y < T.scroll_top)
		return;
	if (T.c.y >= T.scroll_bottom)
		return;
	limit(&n, 0, T.scroll_bottom - T.c.y);
	if (!n)
		return;
	// scroll lines up
	shift_rows(T.c.y, T.scroll_bottom, -n);
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
	limit(&n, 0, T.width-T.c.x);
	clear_region(T.c.x, T.c.y, T.c.x+n, T.c.y+1);
}

void select_charset(int g, Char set) {
	if (g>=0 && g<4)
		T.charsets[g] = set;
}

void switch_buffer(bool alt) {
	T.current = &T.buffers[alt];
	dirty_all();
}

void save_cursor(void) {
	T.saved_cursor = T.c;
}

void restore_cursor(void) {
	T.c = T.saved_cursor;
	cursor_to(T.c.x, T.c.y);
}

void set_scroll_region(int y1, int y2) {
	// behavior taken from xterm
	if (y2 < y1)
		return;
	limit(&y1, 0, T.height-1);
	limit(&y2, 0, T.height);
	T.scroll_top = y1;
	T.scroll_bottom = y2;
	cursor_to(0, 0); // where is this supposed to move the cursor?
}
