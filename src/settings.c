// note: this is NOT a configuration file!
// it just contains functions for loading settings, and their default values
// see `xresources-example.ad` for more information

#include <X11/Xresource.h>

#include "buffer.h"
#include "settings.h"
#include "x.h"
extern bool parse_x_color(const char* c, RGBColor* out);

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
	.faceName = "comic mono",
	.faceSize = 12,
	.hyperlinkCommand = "xdg-open",
	.termName = "xterm-12term",
};

XrmDatabase	db = NULL;

static bool get_string(char* name, char** out) {
	XrmValue ret;
	char* type;
	if (db && XrmGetResource(db, name, "String", &type, &ret)) {
		// do we need to duplicate this 
		*out = ret.addr;
		return true;
	}
	return false;
}

static bool get_color(char* name, RGBColor* out) {
	char* str;
	if (get_string(name, &str))
		if (parse_x_color(str, out))
			return true;
	return false;
}

static bool get_number(char* name, double* out) {
	char* str;
	if (get_string(name, &str)) {
		char* end;
		double n = strtod(str, &end);
		if (str[0]!='\0' && *end=='\0') {
			*out = n;
			return true;
		}
	}
	return false;
}

static bool get_integer(char* name, int* out) {
	double d;
	if (get_number(name, &d)) {
		*out = (int)d;
		return true;
	}
	return false;
}

#define FIELD(name) "12term." #name, &settings.name

void load_settings(void) {
	char* resource_manager = XResourceManagerString(W.d);//screen?
	if (resource_manager) {
		if (db)
			XrmDestroyDatabase(db);
		db = XrmGetStringDatabase(resource_manager);
	}
	
	get_string(FIELD(faceName));
	get_number(FIELD(faceSize));
	get_color(FIELD(cursorColor));
	get_color(FIELD(background));
	get_color(FIELD(foreground));
	get_integer(FIELD(saveLines));
	get_string(FIELD(termName));
	for (int i=0; i<16; i++) {
		char buf[100];
		sprintf(buf, "12term.color%d", i);
		get_color(buf, &settings.palette[i]);
	}
	// non-xterm
	get_integer(FIELD(width));
	get_integer(FIELD(height));	
	get_string(FIELD(hyperlinkCommand));
	if (settings.hyperlinkCommand[0]=='\0')
		settings.hyperlinkCommand = NULL;
	get_integer(FIELD(cursorShape));
}
