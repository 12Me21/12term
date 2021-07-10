#include "buffer.h"
#include "settings.h"

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
RGBColor default_foreground = {  0,  0,  0};
RGBColor default_background = {255,255,255};
int default_cursor_style = 2;

int default_width = 80;
int default_height = 24;

const char* default_font = "cascadia code,fira code,monospace:size=12:antialias=true:autohint=true";
