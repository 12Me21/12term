#pragma once

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>

#include "common.h"

// globals
typedef struct Xw {
	Display* d;
	int scr;
	Visual* vis;
	Colormap cmap;
	Window win;
	Pixmap pix;
	GC gc;
	XftDraw* draw;
	XSetWindowAttributes attrs;
	
	// for rendering the cursor
	Pixmap under_cursor;
	
	Px w,h;
	Px cw,ch;
	Px border;
	float cwscale, chscale;// todo
	
	bool ligatures;
	
	int font_ascent;
	
	struct atoms {
		Atom xembed;
		Atom wm_delete_window;
		Atom net_wm_name;
		Atom net_wm_icon_name;
		Atom net_wm_pid;
		Atom utf8_string;
		Atom clipboard;
		Atom incr;
	} atoms;
} Xw;

extern Xw W;

void sleep_forever(bool hangup);
void clippaste(void);
