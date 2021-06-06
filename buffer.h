#pragma once
#include <stdint.h>
#include <stdbool.h>
#define index index_

//#include "coroutine.h"

// unicode character
typedef int32_t Char;

typedef struct RGBColor {
	unsigned char r,g,b;
} RGBColor;

// if .truecolor is true, .rgb is the color.
// otherwise, .i is the palette index (-1 = default foreground, -2 = default background)
typedef struct Color {
	union {
		RGBColor rgb;
		short i;
	};
	bool truecolor;
} Color;

// display attributes for characters
typedef struct Attrs {
	Color color, background;
	
	char weight: 2; // 1 = bold, -1 = faint
	bool italic: 1;
	bool underline: 1;
	bool blink: 1;
	bool reverse: 1;
	bool invisible: 1;
	bool strikethrough: 1;
} Attrs;

// single character cell
typedef struct Cell {
	Char chr;
	Char combining[16]; // null terminated list
	Attrs attrs;
	char wide; //0: normal. 1:wide(left). -1:wide(right)
} Cell;

// compressed version of cells, used for scrollback buffer (todo)
// combining chars will be stored as separate cells
typedef struct ScrollbackCell {
	Char chr;
	Attrs attrs;
} ScrollbackCell;

typedef Cell* Row;

// the cursor keeps track of a position as well as the attributes
typedef struct Cursor {
	int x,y; // 0-indexed
	Attrs attrs;
} Cursor;

// the main or alternate buffer.
typedef struct Buffer {
	Row* rows;
	int scroll_top; // inclusive
	int scroll_bottom; // exclusive
} Buffer;

// all terminal properties
typedef struct Term {
	int width, height;
	
	Buffer buffers[2]; // main, alt
	Buffer* current; // always points to an item in .buffers
	// todo: are these per-buffer?
	
	Cursor c;
	bool show_cursor;
	bool blink_cursor;
	Cursor saved_cursor;
	
	RGBColor background, foreground; // these can maybe be accessed as palette[-1] and [-2] but don't try it lol
	RGBColor palette[256];
	
	int* tabs; ///todo
	// this is stored as a lookup table where .tabs[x] is the column to jump to when recieving a tab in column x.
	// for example, by default it will look like {8,8,8,8,8,8,8,16,16,16...}
	
	bool bracketed_paste;
	
	bool* dirty_rows;
	
	struct scrollback {
		Row* rows;
		int size;
		int lines;
	} scrollback;
	
	struct parse {
		char string[1030];
		int string_command;
		int string_length;
		int state;
		int argv[100];
		bool arg_colon[100]; //todo
		int argc;
		bool csi_private;
		int charset;
	} parse;
} Term;

void draw_screen(Term* t);
void init_term(Term* t, int width, int height);
int term_write(Term* t, int len, char buf[len]);
void put_char(Term* t, Char c);
void index(Term* t, int amount);
void backspace(Term* t);
void clear_region(Term* t, int x1, int y1, int x2, int y2);

int cursor_up(Term* t, int amount);
int cursor_down(Term* t, int amount);
void cursor_right(Term* t, int amount);
void cursor_left(Term* t, int amount);
void cursor_to(Term* t, int x, int y);
void delete_chars(Term* t, int n);
void insert_blank(Term* t, int n);
void term_resize(Term* t, int width, int height);
void delete_lines(Term* t, int n);
