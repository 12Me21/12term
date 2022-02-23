#pragma once
// Definitions for structures related to the screen buffer

#include "common.h"

// todo: maybe make a separate cell.h file or something

typedef struct RGBColor {
	uint8_t r,g,b;
} RGBColor;

// if .truecolor is true, .rgb is the color.
// otherwise, .i is the palette index (-1 = default foreground, -2 = default background)
// normally this would be written as:
// struct {
//  union {
//   RGBColor rgb;
//   int16_t i;
//  };
//  bool truecolor;
// };
// but: this would use 6 bytes instead of 4 due to padding and alignment
typedef union Color {
	struct {
		RGBColor rgb;
		bool truecolor;
	};
	int16_t i;
} Color;
// RRRRRRRR GGGGGGGG BBBBBBBB 1.......
// iiiiiiii iiiiiiii ........ 0.......

// display attributes for characters
typedef struct Attrs {
	Color color, background, underline_color;
	uint16_t link; // hyperlink. 0 = none, 1…max = T.links.items[n-1]
	
	int8_t weight: 2; // 0 = normal, 1 = bold, -1 = faint
	bool italic: 1;
	uint8_t underline: 3; // 0 = none, 1-5 = normal,double,wavy,dotted,dashed (todo: support more of these)
	bool colored_underline: 1; // whether to use special underline color
	bool blink: 1;
	bool reverse: 1;
	// note: the renderer doesn't actually use the .reverse and .bold to determine color: these transformations are done when writing text to the screen.
	bool strikethrough: 1;
	
	bool invisible: 1; // todo?
} Attrs;

// single character cell
typedef struct Cell {
	Char chr;
	Char combining[1]; //todo: (and make this like, 4 or something)
	Attrs attrs;
	int8_t wide: 2; //0 = normal, 1 = left half of wide char, -1 = right half (chr=0)
// fullwidth chars consist of 2 cells:
// - a cell with wide=1, and the character data stored in it
// - a cell with wide=-1, and no data
} Cell;

// TODO: make a compressed version of this for storing history cells?

typedef struct Row {
	// TODO:
	// when printing a char causes the cursor to wrap to the next line,
	// the `wrap` flag is set on the old line, and `cont` is set on the new line
	// (TODO) when the terminal is resized, lines are spliced together if:
	// a line has the `wrap` flag set, AND the following line as the `cont` flag set
	// they are then re-wrapped, with the splits marked using the same flags.
	//int length; // where newline
	bool wrap, cont;
	Cell cells[]; // allocated after struct
} Row;

// the cursor keeps track of a position as well as the attributes
#define Cursor Cursor_
typedef struct Cursor {
	int x; // 0 … width (note: NOT width-1! cursor can be in the column past the right edge of the screen)
	int y; // 0 … height-1
	Attrs attrs;
} Cursor;

// the main or alternate buffer.
typedef struct Buffer {
	Row** rows;
} Buffer;

// all terminal properties
typedef struct Term {
	int width, height; // must be > 0
	
	Buffer buffers[2]; // main, alt
	Buffer* current; // always points to an item in .buffers
	
	int scroll_top; // inclusive
	int scroll_bottom; // exclusive
	// scroll_bottom > scroll_top
	// wait, is that true?
	
	Cursor c;
	Cursor saved_cursor;
	bool show_cursor;
	int cursor_shape;
	bool cursor_blink; // unused, probably will never implement this because it sucks lol
	
	// whether there was a character just printed, which combining chars can be added to:
	bool last;
	// position of that char
	int last_x; // 0 … T.width-1 (never offscreen)
	int last_y; // 0 … T.height-1
	
	RGBColor cursor_color;
	RGBColor background, foreground; // these can maybe be accessed as palette[-1] and [-2] but don't try it lol
	RGBColor palette[256];
	
	bool* tabs; // if we get more of these structures that store info per column/row we might want to make a dedicated "column" and "row" structure hmm? and also 
	// also yes, tabs are not stored per-buffer.
	// and some programs assume that tab stops are set to every 8 columns
	// and don't bother checking or resetting these
	// so honestly idk if even allowing setting them is a good idea...
	
	int scroll;
	
	int charsets[4];
	
	struct links {
		int length;
		utf8* items[32767];
	} links;
	
	bool app_keypad, app_cursor;
	bool bracketed_paste;
	int mouse_mode;
	int mouse_encoding;
	bool report_focus; //todo
} Term;

void init_term(int width, int height);
void term_resize(int width, int height);
void set_scrollback(int pos);
bool move_scrollback(int amount);
void dirty_all(void);
Row* get_row(int y);
Row* resize_row(Row** row, int size, int old_size);

extern Term T;
