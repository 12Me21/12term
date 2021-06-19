#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

// (these are macros because they calculate the size based on the type of the variable)
#define ALLOC(var, length) (var) = malloc(sizeof(*(var)) * (length))
#define REALLOC(var, length) (var) = realloc((var), sizeof(*(var)) * (length))

#define LEN(var) (sizeof(var)/sizeof((var)[0]))

// unicode character
// this is SIGNED since unicode only uses 21 of the 32 bits, so we can use negatives to easily represent invalid chars etc.
typedef int32_t Char;

typedef int Px;

#include "debug.h"
