#pragma once
#include <X11/XKBlib.h>
#include <X11/keysym.h>

typedef void (*KeyFunc)(void);

typedef struct KeyMap {
	KeySym k;
	char modifiers;
	
	const char* output; // format string to print (when mode!=10)
	
	char mode;
	int arg; // argument to printf
	
	KeyFunc func; // function to call (when mode=10)
	
	char app_cursor;
	char app_keypad;
} KeyMap;

extern KeyMap* KEY_MAP;
