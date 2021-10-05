#pragma once
#include "buffer.h"
#include <fontconfig/fontconfig.h>

typedef struct Settings {
	// only the first 16 colors can be customized; the rest are just included here for convenience and consistency.
	// todo: maybe include the other 3 colors in this array too.
	RGBColor palette[256];
	RGBColor cursorColor;
	RGBColor foreground;
	RGBColor background;
	int cursorShape;
	int width;
	int height;
	utf8* faceName;
	double faceSize;
	utf8* hyperlinkCommand;
	utf8* termName;
	int saveLines;
	
	struct {
		bool antialias;
		bool embolden;
		bool hinting;
		int hint_style;
		bool autohint;
		int rgba;
		int lcd_filter;
		bool minspace;
		double dpi;
		double scale;
		int max_glyph_memory;
	} xft;
} Settings;

extern Settings settings;

void load_settings(int* argc, char** argv);
void pattern_default_substitute(FcPattern* pattern);
