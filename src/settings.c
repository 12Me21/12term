// note: this is NOT a configuration file!
// it just contains functions for loading settings, and their default values
// see `xresources-example.ad` for more information

#include <X11/Xresource.h>
#include <X11/extensions/Xrender.h>
#include <fontconfig/fontconfig.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "buffer.h"
#include "settings.h"
#include "x.h"
#include "xft/Xft.h"
extern bool parse_x_color(const utf8* c, RGBColor* out);

Settings settings = {
	.palette = {
	  // dark colors
		{  0,  0,  0}, // dark black
		{170,  0,  0}, // dark red
		{  0,170,  0}, // dark green
		{170, 85,  0}, // dark yellow
		{  0,  0,170}, // dark blue
		{170,  0,170}, // dark magenta
		{  0,170,170}, // dark cyan
		{170,170,170}, // dark white
		// light colors
		{ 85, 85, 85}, // light black
		{255, 85, 85}, // light red
		{ 85,255, 85}, // light green
		{255,255, 85}, // light yellow
		{ 85, 85,255}, // light blue
		{255, 85,255}, // light magenta
		{ 85,255,255}, // light cyan
		{255,255,255}, // light white
	},
	.cursorColor = {  0,192,  0},
	.foreground = {255,255,255},
	.background = {  0,  0,  0},
	.cursorShape = 2,
	.saveLines = 2000,
	.width = 80,
	.height = 24,
	.faceName = "monospace",
	.faceSize = 12,
	.hyperlinkCommand = "xdg-open",
	.termName = "xterm-12term",
};

XrmDatabase	db = NULL;

static bool get_string(utf8* name, utf8** out) {
	XrmValue ret;
	utf8* type;
	if (db && XrmGetResource(db, name, "", &type, &ret)) { //I tried passing NULL for the class string but it didn't like that,
		if (strcmp(type, "String"))
			return false;
		// do we need to duplicate this 
		*out = ret.addr;
		return true;
	}
	return false;
}

static bool get_color(utf8* name, RGBColor* out) {
	utf8* str;
	if (get_string(name, &str))
		if (parse_x_color(str, out))
			return true;
	return false;
}

static bool get_number(utf8* name, double* out) {
	utf8* str;
	if (get_string(name, &str)) {
		utf8* end;
		double n = strtod(str, &end);
		if (str[0]!='\0' && *end=='\0') {
			*out = n;
			return true;
		}
	}
	return false;
}

static bool get_integer(utf8* name, int* out) {
	utf8* str;
	if (get_string(name, &str)) {
		
		// this converts fontconfig constant strings into integers
		// i.e. subpixel types like "rgb", "bgr" become 1, 2, etc.
		// very important
		if (FcNameConstant((FcChar8*)str, out))
			return true;
		
		utf8* end;
		int n = strtol(str, &end, 0);
		if (str[0]!='\0' && *end=='\0') {
			*out = n;
			return true;
		}
	}
	return false;
}

static bool get_boolean(utf8* name, bool* out) {
	utf8* str;
	if (get_string(name, &str)) {
		utf8 c0 = str[0];
		if (isupper(c0))
			c0 = tolower(c0);
		if (c0 == 't' || c0 == 'y' || c0 == '1') {
			*out = true;
			return true;
		}
		if (c0 == 'f' || c0 == 'n' || c0 == '0') {
			*out = false;
			return true;
		}
		if (c0 == 'o') {
			utf8 c1 = str[1];
			if (isupper(c1))
				c1 = tolower(c1);
			if (c1 == 'n') {
				*out = true;
				return true;
			}
			if (c1 == 'f') {
				*out = false;
				return true;
			}
		}
	}
	return false;
}

#define FIELD(name) "12term." #name, &settings.name

void load_settings(int* argc, utf8** argv) {
	utf8* resource_manager = XResourceManagerString(W.d);//screen?
	if (resource_manager) {
		if (db)
			XrmDestroyDatabase(db);
		db = XrmGetStringDatabase(resource_manager);
	}
	// todo: finish this
	//XrmOptionDescRec option_desc[] = {
		//	{"-fa", ".faceName", XrmoptionSepArg}
		//};
	//XrmParseCommand(&db, option_desc, LEN(option_desc), "12term", argc, argv);
	
	get_string(FIELD(faceName));
	get_number(FIELD(faceSize));
	get_color(FIELD(cursorColor));
	get_color(FIELD(background));
	get_color(FIELD(foreground));
	get_integer(FIELD(saveLines));
	get_string(FIELD(termName));
	// first 16 palette colors
	for (int i=0; i<16; i++) {
		utf8 buf[100];
		sprintf(buf, "12term.color%d", i);
		get_color(buf, &settings.palette[i]);
	}
	// init the rest of the palette
	int p = 16;
	// 6x6x6 rgb cube
	const int brightness[6] = {0, 95, 135, 175, 215, 255};
	for (int i=0; i<6*6*6; i++) {
		settings.palette[p++] = (RGBColor){
			brightness[i/6/6 % 6],
			brightness[i/6 % 6],
			brightness[i % 6],
		};
	}
	// fill the rest with grayscale
	for (int i=0; i<256-16-6*6*6; i++) {
		settings.palette[p++] = (RGBColor) {
			8 + 10*i, 8 + 10*i, 8 + 10*i,
		};
	}
	
	// non-xterm
	get_integer(FIELD(width));
	get_integer(FIELD(height));	
	get_string(FIELD(hyperlinkCommand));
	if (settings.hyperlinkCommand[0]=='\0')
		settings.hyperlinkCommand = NULL;
	get_integer(FIELD(cursorShape));
	
	// xft
	settings.xft.antialias = true;
	get_boolean("Xft." FC_ANTIALIAS, &settings.xft.antialias);
	settings.xft.embolden = false;
	get_boolean("Xft." FC_EMBOLDEN, &settings.xft.embolden);
	settings.xft.hinting = true;
	get_boolean("Xft." FC_HINTING, &settings.xft.hinting);
	settings.xft.hint_style = FC_HINT_FULL;
	get_integer("Xft." FC_HINT_STYLE, &settings.xft.hint_style);
	settings.xft.autohint = FC_AUTOHINT;
	get_boolean("Xft." FC_AUTOHINT, &settings.xft.autohint);
	
	if (!get_integer("Xft." FC_RGBA, &settings.xft.rgba)) {
		// on my computer, xrenderquerysubpixelorder just returns 0
		// i assume it's meant to be configured somewhere but idk
		int render_order = XRenderQuerySubpixelOrder(W.d, W.scr);
		int subpixel;
		switch (render_order) {
		default:
		case SubPixelUnknown: subpixel = FC_RGBA_UNKNOWN; break;
		case SubPixelHorizontalRGB: subpixel = FC_RGBA_RGB; break;
		case SubPixelHorizontalBGR: subpixel = FC_RGBA_BGR; break;
		case SubPixelVerticalRGB: subpixel = FC_RGBA_VRGB; break;
		case SubPixelVerticalBGR: subpixel = FC_RGBA_VBGR; break;
		case SubPixelNone: subpixel = FC_RGBA_NONE; break;
		}
		settings.xft.rgba = subpixel;
	}
	
	settings.xft.lcd_filter = FC_LCD_DEFAULT;
	get_integer("Xft." FC_LCD_FILTER, &settings.xft.lcd_filter);
	settings.xft.minspace = false;
	get_boolean("Xft." FC_MINSPACE, &settings.xft.minspace);
	if (!get_number("Xft." FC_DPI, &settings.xft.dpi)) {
		settings.xft.dpi = (double)DisplayHeight(W.d, W.scr)*25.4 / DisplayHeightMM(W.d, W.scr);
	}
	settings.xft.scale = 1;
	get_number("Xft." FC_SCALE, &settings.xft.scale);
	settings.xft.max_glyph_memory = 1024*1024;
	get_integer("Xft." XFT_MAX_GLYPH_MEMORY, &settings.xft.max_glyph_memory);
}

static bool pattern_missing(FcPattern* pattern, const utf8* name) {
	FcValue v;
	return FcPatternGet(pattern, name, 0, &v)==FcResultNoMatch;
}

static void pattern_default_bool(FcPattern* pattern, utf8* field, bool value) {
	if (pattern_missing(pattern, field))
		FcPatternAddBool(pattern, field, value);
}

static void pattern_default_integer(FcPattern* pattern, utf8* field, int value) {
	if (pattern_missing(pattern, field))
		FcPatternAddInteger(pattern, field, value);
}

static void pattern_default_number(FcPattern* pattern, utf8* field, double value) {
	if (pattern_missing(pattern, field))
		FcPatternAddDouble(pattern, field, value);
}

// 
void pattern_default_substitute(FcPattern* pattern) {
	pattern_default_bool(pattern, FC_ANTIALIAS, settings.xft.antialias);
	pattern_default_bool(pattern, FC_EMBOLDEN, settings.xft.embolden);
	pattern_default_bool(pattern, FC_HINTING, settings.xft.hinting);
	pattern_default_integer(pattern, FC_HINT_STYLE, settings.xft.hint_style);
	pattern_default_bool(pattern, FC_AUTOHINT, settings.xft.autohint);
	pattern_default_integer(pattern, FC_RGBA, settings.xft.rgba);
	pattern_default_integer(pattern, FC_LCD_FILTER, settings.xft.lcd_filter);
	pattern_default_bool(pattern, FC_MINSPACE, settings.xft.minspace);
	pattern_default_number(pattern, FC_DPI, settings.xft.dpi);
	pattern_default_number(pattern, FC_SCALE, settings.xft.scale);
	pattern_default_integer(pattern, XFT_MAX_GLYPH_MEMORY, settings.xft.max_glyph_memory);
	FcDefaultSubstitute(pattern);
}
