#pragma once

#include "common.h"

extern bool debug_enabled;

__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) void die(const char *errstr, ...);
void time_log(const char* str);
void print(const char* str, ...) __attribute__((format(printf, 1, 2)));
const char* char_name(Char c);

typedef union {
	char item_0;
	struct {
		char open, openv, render, draw, ref, glyph, glyphv, cache, cachev, memory; // not all are used anymore...
		char redraw, dirty, utf8; //mine
	};
} Debug_options;

extern Debug_options DEBUG;

void debug_init(void);
