#pragma once
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include "common.h"

typedef void (*KeyFunc)(void);

typedef struct KeyMap {
	KeySym k;
	int8_t modifiers;
	
	const utf8* output; // format string to print (when mode!=10)
	
	int8_t mode;
	int arg; // argument to printf
	
	KeyFunc func; // function to call (when mode=10)
	
	int8_t app_cursor;
	int8_t app_keypad;
} KeyMap;

extern KeyMap KEY_MAP[];
