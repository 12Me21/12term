// this file deals with general interfacing with X and contains the main function and main loop

#define _POSIX_C_SOURCE 200112L
#include <math.h>
#include <unistd.h>
#include <locale.h>
#include <errno.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>
#include <X11/xpm.h>

#include "common.h"
#include "tty.h"
#include "draw.h"
#include "x.h"
#include "font.h"
#include "buffer.h"
#include "event.h"

extern char* ICON_XPM[];

Xw W = {0};

// hhhh
// todo: output some kind of status for invalid?
RGBColor parse_x_color(const char* c) {
	XColor ret;
	XParseColor(W.d, W.cmap, c, &ret);
	return (RGBColor){
		ret.red*255/65535,
		ret.green*255/65535,
		ret.blue*255/65535,
	};
}

// so we need to clean up these size change functions
// basically, there are a few cases:
// 1: on init, we update both the window size and the char size
// 2: when the font changes, we update the char size and probably the window size too? (do we preserve the window size or??)
// 3: when the window is resized, we update the window size only.

// updating the char size is the main expensive operation, as it requires clearing the font cache (or whatever).

// so anyway how about this:
// when the window is resized, we call a function which just updates the total size.
// on init, and when switching fonts, we call another function which updates both.

// this is called when changing the window size
// set `charsize` if W.cw or W.ch have changed.
void change_size(Px w, Px h, bool charsize) {
	Px base = W.border*2;
	int width = (w-base) / W.cw;
	int height = (h-base) / W.ch;
	W.w = w;
	W.h = h;
	if (charsize) {
		XSetWMNormalHints(W.d, W.win, &(XSizeHints){
			.flags = PSize | PResizeInc | PBaseSize | PMinSize,
			.width = W.w,
			.height = W.h,
			.width_inc = W.cw,
			.height_inc = W.ch,
			.base_width = base,
			.base_height = base,
			.min_width = base + W.cw*2,
			.min_height = base + W.ch*2,
		});
	}
	tty_resize(width, height, width*W.cw, height*W.ch);
	term_resize(width, height);
	draw_resize(width, height, charsize);
}

__attribute__((noreturn)) void sleep_forever(bool hangup) {
	print("goodnight...\n");
	
	//if (hangup)
	tty_hangup();
	
	fonts_free();
	
	draw_free();
	//FcFini(); // aaa
	
	XCloseDisplay(W.d);
	
	_exit(0); //is this right?
}

void clipboard_copy() {
	
}

static int timediff(struct timespec t1, struct timespec t2) {
	return (t1.tv_sec-t2.tv_sec)*1000 + (t1.tv_nsec-t2.tv_nsec)/1E6;
}

static double minlatency = 8;
static double maxlatency = 33;

// todo: clean this up
static void run(void) {
	XEvent ev;
	int w = W.w, h = W.h;
	do {
		XNextEvent(W.d, &ev);
		if (XFilterEvent(&ev, None))
			continue;
		if (ev.type == ConfigureNotify) {
			w = ev.xconfigure.width;
			h = ev.xconfigure.height;
		}
	} while (ev.type != MapNotify);
	
	change_size(w, h, true);
	
	time_log("window mapped");
	
	float timeout = -1;
	struct timespec trigger = {0};
	bool drawing = false;
	bool got_text = false, got_draw = false; //just for logging
	bool readed = false;
	
	while (1) {
		Fd xfd = XConnectionNumber(W.d); // do we need to check this every time?
		
		if (XPending(W.d))
			timeout = 0;
		
		bool text = tty_wait(xfd, timeout);
		
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		
		// handle text
		if (text) {
			if (!got_text) {
				time_log("first text");
				got_text = true;
			}
			readed = true;
			tty_read();
		}
		
		// handle x events
		bool xev = false;
		while (XPending(W.d)) {
			xev = true;
			XNextEvent(W.d, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			if (HANDLERS[ev.type])
				(HANDLERS[ev.type])(&ev);
		}
		
		// I don't exactly understand how this timeout delay system works.
		// needs to be rewritten maybe.
		// Things which should delay redrawing:
		// - when there are new lines scrolled into the screen which haven't been written to yet
		// - when cursor is hidden
		if (text || xev) {
			if (!drawing) {
				trigger = now;
				drawing = true;
			}
			timeout = (float)(maxlatency - timediff(now, trigger)) / maxlatency * minlatency;
			if (timeout > 0)
				continue;
		}
		timeout = -1;
		//if (!T.show_cursor)
		//	continue;
		
		if (readed) {
			if (!got_draw) {
				time_log("first draw");
				got_draw = true;
			}
			
			draw();
			if (T.show_cursor)
				xim_spot(T.c.x, T.c.y);
			readed = false;
			drawing = false;
		}
	}
}

static void init_atoms(void) {
	char* ATOM_NAMES[] = {
		"_XEMBED", "WM_DELETE_WINDOW", "_NET_WM_NAME", "_NET_WM_ICON_NAME", "_NET_WM_PID", "UTF8_STRING", "CLIPBOARD", "INCR",
	};
	XInternAtoms(W.d, ATOM_NAMES, LEN(ATOM_NAMES), False, &W.atoms_0);
	if (!W.atoms.utf8_string) // is this even like, possible?
		W.atoms.utf8_string = XA_STRING;
}

extern RGBColor default_background; //messy messy messy

// TODO: clean up startup process
// 1: set locale
// 2: start the shell process (so we can let the shell start up while the term is initializing
// 3: open x connection

// 4: load fonts
// -- now we know the character cell size --

// 5: create and set up the window
// 6: do everything that doesn't depend on window size

// 6: wait for window mapping event
// -- now we know the window size --
// ok but really we knew it before hh

// 7: initialize everything else
// 8: start main loop


void set_title(char* s) {
	if (!s)
		s = "12term"; // default title
	XSetWMName(W.d, W.win, &(XTextProperty){
		(unsigned char*)s, W.atoms.utf8_string, 8, strlen(s)
	});
}

int main(int argc, char* argv[argc+1]) {
	time_log(NULL);
	
	// hecking locale
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	
	// default size (todo)
	int w = 50;
	int h = 10;
	
	W.border = 3;
	
	//W.ligatures = true;
	
	W.d = XOpenDisplay(NULL);
	if (!W.d)
		die("Could not connect to X server\n");
	W.scr = XDefaultScreen(W.d);
	W.vis = XDefaultVisual(W.d, W.scr);
	
	time_log("x stuff 1");
	
	tty_init(); // todo: maybe try to pass the window size here if we can guess it?
	
	init_atoms();
	
	FcInit();
	// todo: this
	init_fonts("cascadia code,fira code,monospace:pixelsize=16:antialias=true:autohint=true", 0);
	
	// messy messy
	W.w = W.cw*w+W.border*2;
	W.h = W.ch*h+W.border*2;
	
	W.cmap = XDefaultColormap(W.d, W.scr);
	
	// create the window
	
	unsigned long bg_pixel = alloc_color((Color){.truecolor=true,.rgb=default_background}); // yuck
	
	W.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask | ExposureMask | VisibilityChangeMask | StructureNotifyMask | ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	
	W.win = XCreateWindow(W.d, XRootWindow(W.d, W.scr),
		0, 0, W.w, W.h, // geometry
		0, // border width
		XDefaultDepth(W.d, W.scr), // depth
		InputOutput, // class
		W.vis, // visual
		// attributes:
		CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap,
		&(XSetWindowAttributes){
			.background_pixel = bg_pixel,
			.border_pixel = bg_pixel,
			.bit_gravity = NorthWestGravity,
			.event_mask = W.event_mask,
			.colormap = W.cmap,
		}
	);
	
	// allow listening for window close event
	XSetWMProtocols(W.d, W.win, &W.atoms.wm_delete_window, 1);
	// set _NET_WM_PID property
	XChangeProperty(W.d, W.win, W.atoms.net_wm_pid, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&(pid_t){getpid()}, 1);
	
	// set title and icon
	XSetClassHint(W.d, W.win, &(XClassHint){
		.res_name = "12term",
		.res_class = "12term",
	});
	set_title(NULL);
	
	Pixmap icon_pixmap;
	Pixmap mask = 0;
	XpmCreatePixmapFromData(W.d, W.win, ICON_XPM, &icon_pixmap, &mask, 0);
	
	XSetWMHints(W.d, W.win, &(XWMHints){
		.flags = InputHint | IconPixmapHint,
		.input = true, // which input focus model
		.icon_pixmap = icon_pixmap,
	});
	
	// init other things
	init_draw();
	
	init_input();
	
	XMapWindow(W.d, W.win);
	
	time_log("created window");
	
	init_term(w, h); // todo: we are going to get a term_resize event quickly after this, mmm
	
	run();
	return 0;
}

void change_font(const char* name) {
	init_fonts(name, 0);
	change_size(W.w, W.h, true);
}
