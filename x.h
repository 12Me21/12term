#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>
typedef int Px;

#define Font Font_
typedef struct {
	Px height;
	Px width;
	Px ascent;
	Px descent;
	bool badslant;
	bool badweight;
	short lbearing;
	short rbearing;
	XftFont* match;
	FcFontSet* set;
	FcPattern* pattern;
} Font;

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
	
	// for rendering the cursor
	Pixmap under_cursor;
	bool cursor_drawn;
	int cursor_x, cursor_y;
	int cursor_width;
	
	Px w,h;
	Px cw,ch;
	Px border;
	float cwscale, chscale;// todo
	
	Font fonts[4]; // normal, bold, italic, bold+italic
	
	struct atoms {
		Atom xembed;
		Atom wm_delete_window;
		Atom net_wm_name;
		Atom net_wm_icon_name;
		Atom net_wm_pid;
		Atom utf8_string;
		Atom clipboard;
	} atoms;
	
	struct {
		XIM xim;
		XIC xic;
		XPoint spot;
		XVaNestedList spotlist;
	} ime;
} Xw;

extern Xw W;
