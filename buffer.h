#pragma once
#include <stdint.h>

typedef int32_t Char;

typedef struct RGBColor {
	unsigned char r,g,b;
} RGBColor;

// if .indexed is true, .i is the palette index
// -1 = default foreground, -2 = default background
// otherwise, .r,.g,.b are the color channels
typedef struct Color {
	union {
		RGBColor rgb;
		short i;
	};
	char truecolor;
} Color;

// display attributes for characters
typedef struct Attrs {
	Color color, background;
	char bold: 1;
	char faint: 1;
	char italic: 1;
	char underline: 1;
	char blink: 1;
	char reverse: 1;
	char invisible: 1;
	char strikethrough: 1;
	char wide: 1;
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
		int len;
	} scrollback;
	RGBColor palette[256];
	RGBColor foreground, background;
} Term;

void draw_screen(Term* t);
void init_term(Term* t, int width, int height);
