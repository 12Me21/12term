#pragma once
#include "buffer.h"

typedef struct Settings {
	// only the first 16 colors can be customized; the rest are just included here for convenience and consistency.
	RGBColor palette[256];
	RGBColor cursorColor;
	RGBColor foreground;
	RGBColor background;
	int cursorShape;
	int width;
	int height;
	char* faceName;
	double faceSize;
	char* hyperlinkCommand;
	char* termName;
	int saveLines;
} Settings;

extern Settings settings;

void load_settings(int* argc, char** argv);
