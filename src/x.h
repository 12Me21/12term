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
	GC gc;
	Window win;
	
	int event_mask;
	
	Px w,h; // size in pixels (including border)
	Px cw,ch; // size of each character cell
	Px border; // width of border
	float cwscale, chscale;// todo
	
	bool ligatures; // unused
	
	int font_ascent; // n
	
	union {
		Atom atoms_0; // this gives us a pointer to the start of the atoms struct, so we can initialize them all with one function call. (see init_atoms())
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
void change_size(int width, int height, bool charsize, bool do_resize);
void force_redraw(void);
