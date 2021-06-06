#pragma once
#include <X11/XKBlib.h>
#include <X11/keysym.h>

typedef struct KeyMap {
	KeySym k;
	int modifiers;
	const char* output;
	char special;
	int arg;
} KeyMap;

extern KeyMap* KEY_MAP;
