#include "buffer.h"
#include "settings.h"

RGBColor default_palette[16] = {
	//r , g , b 
	{  0,  0,  0},
	{170,  0,  0},
	{  0,170,  0},
	{170, 85,  0},
	{  0,  0,170},
	{170,  0,170},
	{  0,170,170},
	{170,170,170},
	{ 85, 85, 85},
	{255, 85, 85},
	{ 85,255, 85},
	{255,255, 85},
	{ 85, 85,255},
	{255, 85,255},
	{ 85,255,255},
	{255,255,255},
};
RGBColor default_cursor = {  0,192,  0};
RGBColor default_foreground = {  0,  0,  0};
RGBColor default_background = {255,255,255};
int default_cursor_style = 2;

int default_width = 80;
int default_height = 24;
