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
	
	int event_mask;
	
	Pixmap under_cursor; //wish this wasn't here
	Pixmap icon_pixmap;
	
	Px w,h; // size in pixels (including border)
	Px cw,ch; // size of each character cell
	Px border; // width of border
	float cwscale, chscale;// todo
	
	bool ligatures; // unused
	
	int font_ascent; // n
	
	union {
		Atom atoms_array[8];
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
	};
} Xw;

extern Xw W;

__attribute__((noreturn)) void sleep_forever(bool hangup);
void clippaste(void);
void update_size(int width, int height);
