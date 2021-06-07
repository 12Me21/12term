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
	bool reverse: 1; //the renderer doesn't actually read this flag: when writing chars to the screen, the color and background are swapped if this attrib is set.
	
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
#define Cursor Cursor_
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
	
	Cursor c;
	bool show_cursor;
	bool blink_cursor;
	Cursor saved_cursor;
	
	RGBColor cursor_background;
	RGBColor background, foreground; // these can maybe be accessed as palette[-1] and [-2] but don't try it lol
	RGBColor palette[256];
	
	bool* tabs; // if we get more of these structures that store info per column/row we might want to make a dedicated "column" and "row" structure hmm? and also 
	// also yes, tabs are not stored per-buffer.
	// and some programs assume that tab stops are set to every 8 columns
	// and don't bother checking or resetting these
	// so honestly idk if even allowing setting them is a good idea...
	
	bool bracketed_paste;
	
	bool* dirty_rows;
	
	int charsets[4];
	
	struct scrollback {
		Row* rows;
		int size;
		int lines;
	} scrollback;
} Term;

void init_term(int width, int height);
void term_resize(int width, int height);

void dirty_all(void);

extern Term T;
