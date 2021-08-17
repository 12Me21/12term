#pragma once
#include "buffer.h"

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
} Settings;

extern Settings settings;

void load_settings(int* argc, char** argv);
