#include <stdlib.h>

#include "buffer.h"

const RGBColor default_palette[16] = {
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

const RGBColor default_foreground = {  0,  0,  0};
const RGBColor default_background = {255,255,255};

void init_palette(Term* t) {
	t->foreground = default_foreground;
	t->background = default_background;
	// normal 16 indexed colors
	int p = 0;
	for (int i=0; i<16; i++)
		t->palette[p++] = default_palette[i];
	// 6x6x6 rgb cube
	const int brightness[6] = {0, 95, 135, 175, 215, 255};
	for (int i=0; i<6*6*6; i++) {
		t->palette[p++] = (RGBColor){
			brightness[i/6/6 % 6],
			brightness[i/6 % 6],
			brightness[i % 6],
		};
	}
	// fill the rest with grayscale
	for (int i=0; i<256-16-6*6*6; i++) {
		t->palette[p++] = (RGBColor) {
			8 + 10*i, 8 + 10*i, 8 + 10*i,
		};
	}
}

void init_scrollback(Term* t) {
	t->scrollback.size = 0;
	t->scrollback.len = 0;
}

void init_term(Term* t, int width, int height) {
	t->width = width;
	t->height = height;
	t->current = &t->buffers[0];
	t->scrollback.size = 0;
	
	init_palette(t);
	init_scrollback(t);
	
	t->c = (Cursor){
		.x = 0, .y = 0,
		.attrs = {
			.color = (Color){.i = -1},
			.background = (Color){.i = -2},
		},
	};
	t->saved_cursor = t->c;
	
	for (int i=0; i<2; i++) {
		Row* rows = malloc(height*sizeof(Row*));
		t->buffers[i] = (Buffer){
			.rows = rows,
			.scroll_top = 1,
			.scroll_bottom = height,
		};
		for (int y=0; y<height; y++) {
			rows[y] = malloc(width*sizeof(Cell));
			for (int x=0; x<width; x++) {
				rows[y][x] = (Cell){
					.chr = 't',
					.attrs = {
						.color = (Color){.i = -1},
						.background = (Color){.i = -2},
					},
				};
			}
		}
	}
	
	for (int i=0; i<height; i++) {
		
	}
}
