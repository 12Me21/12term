#pragma once
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "debug.h"

#define ALLOC(v, size) (v)=malloc(sizeof(*v) * (size))
#define REALLOC(v, size) (v) = realloc((v), sizeof(*(v)) * (size))

// unicode character
typedef int32_t Char;

typedef int Px;
