// note: this is NOT a configuration file!
// it just contains functions for loading settings, and their default values
// see `xresources-example.ad` for more information

#include <X11/Xresource.h>

#include "buffer.h"
#include "settings.h"
#include "x.h"
extern RGBColor parse_x_color(const char* c);

XrmDatabase	db = NULL;

static bool get_x_resource(char* name, char** out) {
	XrmValue ret;
	char* type;
	if (db && XrmGetResource(db, name, "String", &type, &ret)) {
		*out = ret.addr;
		return true;
	}
	return false;
}

static bool get_resource_color(char* name, RGBColor* out) {
	char* str;
	if (get_x_resource(name, &str)) {
		*out = parse_x_color(str);
		return true;
	}
	return false;
}

void load_settings(void) {
	char* resource_manager = XResourceManagerString(W.d);//screen?
	if (resource_manager) {
		if (db)
			XrmDestroyDatabase(db);
		db = XrmGetStringDatabase(resource_manager);
	}
	
	get_x_resource("12term.faceName", &default_font);
	get_x_resource("12term.hyperlinkCommand", &hyperlink_command);
	get_resource_color("12term.cursorColor", &default_cursor);
	get_resource_color("12term.background", &default_background);
	get_resource_color("12term.foreground", &default_foreground);
	for (int i=0; i<16; i++) {
		char buf[100];
		sprintf(buf, "12term.color%d", i);
		get_resource_color(buf, &default_palette[i]);
	}
}

RGBColor default_palette[16] = {
	//r , g , b 
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
};
RGBColor default_cursor = {  0,192,  0};
RGBColor default_foreground = {255,255,255};
RGBColor default_background = {  0,  0,  0};
int default_cursor_style = 2;

int default_width = 80;
int default_height = 24;

char* default_font = "comic mono:size=12";

char* hyperlink_command = "xdg-open";

char* term_name = "xterm-12term";
