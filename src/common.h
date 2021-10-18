#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

// (these are macros because they calculate the size based on the type of the variable)
#define ALLOC(var, length) (var) = malloc(sizeof(*(var)) * (length))
#define REALLOC(var, length) (var) = realloc((var), sizeof(*(var)) * (length))
#define FREE(var) { free(var); (var) = NULL; }

#define LEN(var) (sizeof(var)/sizeof((var)[0]))

// you must use { } around the body otherwise the highlighter/indenter complains
#define FOR(var, end) for (int var=0; var<end; var++)

static inline int limit(int x, int min, int max) {
	if (x<min)
		return min;
	else if (x>max)
		return max;
	return x;
}

// unicode character
// this is SIGNED since unicode only uses 21 of the 32 bits, so we can use negatives to easily represent invalid chars etc.
typedef int32_t Char;

typedef int Px;

typedef long long Nanosec;

// this should be unsigned char, but for practical reasons I use char (since most functions take char)
typedef char utf8;

#include "debug.h"
