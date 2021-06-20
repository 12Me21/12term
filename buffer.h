#pragma once
// Definitions for structures related to the screen buffer

#include "common.h"

// todo: maybe make a separate cell.h file or something

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
} __attribute__((packed)) Color;

// display attributes for characters
typedef struct Attrs {
	Color color, background, underline_color;
	
	char weight: 2; // 1 = bold, -1 = faint
	bool italic: 1;
	unsigned char underline: 3; // 0 = none, 1-5 = normal,double,wavy,dotted,dashed (todo: support more of these)
	bool colored_underline: 1; // whether to use special underline color
	bool blink: 1;
	bool reverse: 1;
	// note: the renderer doesn't actually use the .reverse and .bold to determine color: these transformations are done when writing text to the screen.
	bool strikethrough: 1;
	
	bool invisible: 1; // todo
} Attrs;

// single character cell
typedef struct Cell {
	Char chr;
	Char combining[1]; //todo: (and make this like, 4 or something)
	Attrs attrs;
	char wide: 2; //0 = normal, 1 = left half of wide char, -1 = right half (chr=0)
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
} Buffer;

// all terminal properties
typedef struct Term {
	int width, height;
	
	Buffer buffers[2]; // main, alt
	Buffer* current; // always points to an item in .buffers
	
	int scroll_top; // inclusive
	int scroll_bottom; // exclusive
	
	Cursor c;
	Cursor saved_cursor;
	bool show_cursor;
	int cursor_shape;
	bool cursor_blink;
	
	RGBColor cursor_color;
	RGBColor background, foreground; // these can maybe be accessed as palette[-1] and [-2] but don't try it lol
	RGBColor palette[256];
	
	bool* tabs; // if we get more of these structures that store info per column/row we might want to make a dedicated "column" and "row" structure hmm? and also 
	// also yes, tabs are not stored per-buffer.
	// and some programs assume that tab stops are set to every 8 columns
	// and don't bother checking or resetting these
	// so honestly idk if even allowing setting them is a good idea...
	
	bool* dirty_rows;
	//bool** dirty_chars;
	
	int charsets[4];
	
	struct scrollback {
		Row* rows;
		int size;
		int lines;
	} scrollback;
	
	bool app_keypad, app_cursor;
	bool bracketed_paste;
} Term;

void init_term(int width, int height);
void term_resize(int width, int height);
void dirty_all(void);

extern Term T;

