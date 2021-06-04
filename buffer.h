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
	
	bool wide: 1;
} Attrs;

// single character cell
typedef struct Cell {
	Char chr;
	Attrs attrs;
} Cell;

typedef Cell* Row;

// the cursor keeps track of a position as well as the attributes
typedef struct Cursor {
	int x,y;
	Attrs attrs;
} Cursor;

// the main or alternate buffer.
typedef struct Buffer {
	Row* rows;
	int scroll_top, scroll_bottom; // scrolling region
} Buffer;

// all terminal properties
typedef struct Term {
	Buffer buffers[2];
	Buffer* current;
	Cursor c;
	Cursor saved_cursor;
	int width, height;
	struct scrollback {
		Row* rows;
		int size;
		int lines;
	} scrollback;
	RGBColor palette[256];
	RGBColor foreground, background;
	
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
