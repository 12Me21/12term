// functions for controlling the text in the screen buffer

#define _XOPEN_SOURCE 600
#include <wchar.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "buffer.h"
#include "ctlseqs.h"
#include "settings.h"
#include "draw2.h"

Term T;

static struct history {
	Row** rows; // array of pointers
	int size; // length of ring buffer
		
	int scroll; // visual scroll position
		
	int length; // number of rows stored currently
	int head; // next empty slot
} history;

static void init_palette(void) {
	T.foreground = settings.foreground;
	T.background = settings.background;
	T.cursor_color = settings.cursorColor;
	memcpy(T.palette, settings.palette, sizeof(T.palette));
}

static void free_history(void) {
	if (history.rows) {
		for (int i=1; i<=history.length; i++)
			FREE(history.rows[(history.head-i+history.size) % history.size]);
	}
}

// clear + init
void init_history(void) {
	free_history();
	
	history.size = settings.saveLines;
	ALLOC(history.rows, history.size);
	
	history.head = 0;
	history.length = 0;
	
	T.scroll = 0;
}

static void clear_row(Row* row, int start, bool bce) {
	for (int i=start; i<T.width; i++) {
		// todo: check for wide char halves!
		row->cells[i] = (Cell){
			.chr=0,
			.attrs = {
				.color = T.c.attrs.color,
				.background = bce ? T.c.attrs.background : (Color){.i=-2},
			},
		};
	}
	row->wrap = false;
	row->cont = false;
}

void term_free(void) {
	for (int i=0; i<2; i++) {
		for (int y=0; y<T.height; y++)
			free(T.buffers[i].rows[y]);
	}
	free(T.tabs);
	free_history();
}

static void incwrap(int* x, int range) {
	(*x)++;
	if (*x >= range)
		*x = 0;
}

// return: a row removed from history, or NULL
// this row is owned by the caller
static Row* pop_history(void) {
	// check length
	if (history.length<=0)
		return NULL;
	// move head backwards
	if (history.head>0)
		history.head--;
	else
		history.head = history.size-1;
	// return item
	history.length--;
	// we don't need to set history.rows[history.head] to NULL, i think
	return history.rows[history.head];
}

// idea: scroll lock support
static void push_history(int y) {
	if (y<0 || y>=T.height)
		return;
	// free oldest item if necessary
	if (history.length == history.size) {
		FREE(history.rows[history.head]);
	} else {
		history.length++;
	}
	// move row into history
	Row* new = T.buffers[0].rows[y];
	history.rows[history.head] = new;
	T.buffers[0].rows[y] = NULL; // set to null so it doesn't get freed
	// move head forward to next slot
	incwrap(&history.head, history.size);
	// adjust scroll offset if we are scrolled up currently
	if (T.scroll>0)
		T.scroll++;
}

// change the number of cells in a Row
// if *row is NULL, it will be allocated (like realloc)
// the return value is the same thing assigned to *row
Row* resize_row(Row** row, int size, int old_size) {
	*row = realloc(*row, sizeof(Row) + sizeof(Cell)*size);
	if (size > old_size)
		clear_row(*row, old_size, true);
	(*row)->wrap = false;
	(*row)->cont = false;
	return *row;
}

// this sets T.width and T.height
// please do NOT change those variables manually
void term_resize(int width, int height) {
	if (width != T.width) {
		int old_width = T.width;
		T.width = width;
		// resize existing rows
		// todo: option to re-wrap text?
		for (int scr=0; scr <= 1; scr++) {
			FOR (y, T.height) {
				resize_row(&T.buffers[scr].rows[y], T.width, old_width);
			}
		}
		// update tab stops
		REALLOC(T.tabs, T.width+1);
		FOR (x, T.width) {
			T.tabs[x] = (x%8 == 0);
		}
		// update cursor position
		T.c.x = limit(T.c.x, 0, T.width); //note this is NOT width-1, since cursor is allowed to be in the right margin
		// resize history rows
		for (int i=1; i<=history.length; i++) {
			Row** row = &history.rows[(history.head-i+history.size) % history.size];
			resize_row(row, T.width, old_width);
		}
	}
	
	int diff = height-T.height;
	//// height decrease ////
	if (height < T.height) { // diff < 0
		// iterate from top to bottom
		int y = 0;
		// upper rows
		for (; y < -diff; y++) {
			// main buffer: put lines into history
			push_history(y);
			// alt buffer: free
			free(T.buffers[1].rows[y]);
		}
		// lower rows: shift upwards
		for (; y<T.height; y++) {
			T.buffers[0].rows[y+diff] = T.buffers[0].rows[y];
			T.buffers[1].rows[y+diff] = T.buffers[1].rows[y];
		}
		// realloc lists of lines
		T.height = height;
		REALLOC(T.buffers[1].rows, height);
		REALLOC(T.buffers[0].rows, height);
		// adjust cursor position
		T.c.y = limit(T.c.y+diff, 0, T.height-1);
		
	} else if (height > T.height) { // height INCREASE (diff > 0)
		// realloc lists of lines
		REALLOC(T.buffers[1].rows, height);
		REALLOC(T.buffers[0].rows, height);
		T.height = height;
		// iterate from bottom to top
		int y = T.height-1;
		// lower rows: shift downwards
		for (; y >= diff; y--) {
			T.buffers[0].rows[y] = T.buffers[0].rows[y-diff];
			T.buffers[1].rows[y] = T.buffers[1].rows[y-diff];
		}
		/// upper rows:
		for (; y>=0; y--) {
			// main buffer: move rows out of history
			Row* r = pop_history();
			T.buffers[0].rows[y] = r;
			if (!r) // history empty; blank row
				resize_row(&T.buffers[0].rows[y], T.width, 0);
			
			// alt buffer: insert blank row
			T.buffers[1].rows[y] = NULL;
			resize_row(&T.buffers[1].rows[y], T.width, 0);
		}
		// adjust cursor down
		T.c.y += diff;
	}
	// todo: how do we handle the scrolling regions?
	T.scroll_top = 0;
	T.scroll_bottom = T.height;
}

void set_cursor_style(int n) {
	if (n==0) {
		n = settings.cursorShape;
		// todo: maybe just ensure that settings.cursorShape is a valid value rather than fixing it here
		if (!(n>0 && n<=8))
			n = 2; // true default
	}
	
	if (n>0 && n<=8) {
		T.cursor_shape = (n-1)/2;
		T.cursor_blink = (n-1)%2==0;
	}
}

void clear_region(int x1, int y1, int x2, int y2) {
	//print("clear region: [%d,%d]-(%d,%d)\n",x1,y1,x2,y2);
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
		Row* row = T.current->rows[y];
		for (int x=x1; x<x2; x++) {
			row->cells[x] = (Cell){
				.chr=0,
				.attrs = {
					.color = T.c.attrs.color,
					.background = T.c.attrs.background,
				},
			};
		}
		// only unset these flags if the region goes to the edge
		if (x1<=0)
			row->cont = false;
		if (x2>=T.width)
			row->wrap = false;
	}
}

// todo: confirm which things are supposed to be reset by this
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
	
	T.charsets[0] = 0; // like, whatever, man
	
	T.bracketed_paste = false;
	T.app_keypad = false;
	T.app_cursor = false;
	T.mouse_mode = 0;
	T.mouse_encoding = 0;
	
	for (int i=0; i<T.links.length; i++)
		free(T.links.items[i]);
	T.links.length = 0;
	
	reset_parser();
}

// todo: um we need to free these??
int new_link(utf8* url) {
	if (T.links.length < LEN(T.links.items))
		if ((T.links.items[T.links.length] = strdup(url)))
			return T.links.length++;
	print("failed to allocate hyperlink\n");
	return -1;
}

// only call this ONCE
// make sure it's after settings are loaded
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
	init_history();
}

// generic array rotate function
static void memswap(int size, uint8_t a[size], uint8_t b[size]) {
	uint8_t temp[size];
	memcpy(temp, a, size);
	memcpy(a, b, size);
	memcpy(b, temp, size);
}
static void rotate(int count, int itemsize, uint8_t data[count][itemsize], int amount) {
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
#define ROTATE(array, length, amount) (rotate((length), sizeof(*(array)), (void*)(array), (amount)))

// shift the rows in [`y1`,`y2`) by `amount` (negative = up, positive = down)
// and clear the "new" lines
static void shift_rows(int y1, int y2, int amount, bool bce) {
	ROTATE(&T.current->rows[y1], y2-y1, amount);
	draw_rotate_rows(y1, y2, amount, false);
	if (amount>0) { // down
		for (int y=y1; y<y1+amount; y++)
			clear_row(T.current->rows[y], 0, bce);
	} else { // up
		for (int y=y2+amount; y<y2; y++)
			clear_row(T.current->rows[y], 0, bce);
	}
	
}

// move text downwards
static void scroll_down_internal(int amount) {
	int y1 = T.scroll_top;
	int y2 = T.scroll_bottom;
	amount = limit(amount, 0, y2-y1);
	shift_rows(y1, y2, amount, true);
}

static void scroll_up_internal(int amount, bool bce) {
	int y1 = T.scroll_top;
	int y2 = T.scroll_bottom;
	amount = limit(amount, 0, y2-y1);
	if (y1==0 && T.current==&T.buffers[0])
		for (int y=y1; y<y1+amount; y++) {
		// if we are on the main screen, and the scroll region starts at the top of the screen, we add the lines to the history list.
			push_history(y);
			// wait but don't we need to clear this?  memory?
			T.current->rows[y] = malloc(sizeof(Row) + sizeof(Cell)*T.width);
		}
	shift_rows(y1, y2, -amount, bce);
}

void cursor_to(int x, int y) {
	// todo: is it ok to move the cursor to the offscreen column?
	T.c.x = limit(x, 0, T.width-1);
	T.c.y = limit(y, 0, T.height-1);
}

// these scroll + move the cursor with the scrolled text
// todo: confirm the cases where these are supposed to move the cursor
void scroll_up(int amount) {
	scroll_up_internal(amount, true);
	if (T.c.y>=T.scroll_top && T.c.y<T.scroll_bottom) {
		amount = limit(amount, 0, T.c.y-T.scroll_top);
		cursor_to(T.c.x, T.c.y-amount);
	}
}

void scroll_down(int amount) {
	scroll_down_internal(amount);
	if (T.c.y>=T.scroll_top && T.c.y<T.scroll_bottom) {
		amount = limit(amount, 0, T.scroll_bottom-1-T.c.y);
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
	Cell* dest = &T.current->rows[y]->cells[x];
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
			if (i+1<LEN(dest->combining))
				dest->combining[i+1] = 0;
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
	if (c<128) { // assume ascii chars are never wide, to avoid calling wcwidth all the time // wait this includes control chars though? todo: dont print those unless we already filter them
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
		Cell* dest = &T.current->rows[y]->cells[x+1];
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
		Cell* dest = &T.current->rows[y]->cells[x-1];
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
			scroll_up_internal(push, T.current==&T.buffers[1]); // note: here, bce is only enabled on the alt screen
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
	
	// wrap
	if (T.c.x+width > T.width) {
		T.current->rows[T.c.y]->wrap = true;
		forward_index(1);
		T.c.x = 0;
		T.current->rows[T.c.y]->cont = true;
	}
	
	Cell* dest = &T.current->rows[T.c.y]->cells[T.c.x];
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
	if (T.c.attrs.weight==1) { // mm we do this after reverse right?
		if (!dest->attrs.color.truecolor) {
			int i = dest->attrs.color.i;
			if (i>=0 && i<8)
				dest->attrs.color.i += 8;
		}
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
	//	if (T.current->rows[T.c.y]->length<T.c.x)
	//		T.current->rows[T.c.y]->length = T.c.x;
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
	n = limit(n, 0, T.width-T.c.x);
	if (!n)
		return;
	Row* line = T.current->rows[T.c.y];
	memmove(&line->cells[T.c.x], &line->cells[T.c.x+n], sizeof(Cell)*(T.width-T.c.x-n));
	clear_row(line, T.width-n, true);
}

void insert_blank(int n) {
	n = limit(n, 0, T.width-T.c.x);
	if (!n)
		return;
	
	int dst = T.c.x + n;
	int src = T.c.x;
	int size = T.width - dst;
	Row* line = T.current->rows[T.c.y];
	memmove(&line->cells[dst], &line->cells[src], size * sizeof(Cell));
	clear_region(src, T.c.y, dst, T.c.y+1);
}

void insert_lines(int n) {
	if (T.c.y < T.scroll_top)
		return;
	if (T.c.y >= T.scroll_bottom)
		return;
	n = limit(n, 0, T.scroll_bottom - T.c.y);
	if (!n)
		return;
	// scroll lines down
	shift_rows(T.c.y, T.scroll_bottom, n, true);
}
	
void delete_lines(int n) {
	if (T.c.y < T.scroll_top)
		return;
	if (T.c.y >= T.scroll_bottom)
		return;
	n = limit(n, 0, T.scroll_bottom - T.c.y);
	if (!n)
		return;
	// scroll lines up
	shift_rows(T.c.y, T.scroll_bottom, -n, true);
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
	n = limit(n, 0, T.width-T.c.x);
	clear_region(T.c.x, T.c.y, T.c.x+n, T.c.y+1);
}

void select_charset(int g, Char set) {
	if (g>=0 && g<4)
		T.charsets[g] = set;
}

void switch_buffer(bool alt) {
	bool prev = T.current==&T.buffers[1];
	if (prev != alt) {
		T.current = &T.buffers[alt];
		if (alt)
			clear_region(0, 0, T.width, T.height);
	}
}

void save_cursor(void) {
	T.saved_cursor = T.c;
}

void restore_cursor(void) {
	T.c = T.saved_cursor;
	// we limit here in case the window was resized between when the cursor was saved and now
	T.c.x = limit(T.c.x, 0, T.width); //note: not width-1!
	T.c.y = limit(T.c.y, 0, T.height-1);
}

void set_scroll_region(int y1, int y2) {
	// behavior taken from xterm
	if (y2 < y1)
		return;
	y1 = limit(y1, 0, T.height-1);
	y2 = limit(y2, 0, T.height);
	T.scroll_top = y1;
	T.scroll_bottom = y2;
	cursor_to(0, 0); // todo: where is this supposed to move the cursor?
}

void set_scrollback(int pos) {
	pos = limit(pos, 0, history.length);
	print("scrolling %d\n", pos);
	int dist = pos-T.scroll;
	if (abs(dist)<T.height)
		draw_rotate_rows(0, T.height, dist, true);
	T.scroll = pos;
}

bool move_scrollback(int amount) {
	int before = T.scroll;
	set_scrollback(T.scroll+amount);
	return before!=T.scroll;
}

// get a row from the current screen (if y ≥ 0) or the history buffer (if y < 0). returns NULL if n is out of range
Row* get_row(int y) {
	if (y>=0 && y<T.height)
		return T.current->rows[y];
	if (y<0 && -y <= history.length) // history is "-1 indexed"
		return history.rows[(history.head+y+history.size) % history.size];
	return NULL;
}
