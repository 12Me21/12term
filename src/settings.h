#pragma once
#include "buffer.h"

extern RGBColor default_palette[16];
extern RGBColor default_cursor;
extern RGBColor default_foreground;
extern RGBColor default_background;
extern int default_cursor_style;

extern int default_width;
extern int default_height;

extern char* default_font;

extern char* hyperlink_command;

extern char* term_name;

void load_settings(void);
