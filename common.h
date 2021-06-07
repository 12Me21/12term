#pragma once

#define ALLOC(v, size) (v)=malloc(sizeof(*v) * (size))
#define REALLOC(v, size) (v) = realloc((v), sizeof(*(v)) * (size))
