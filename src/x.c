// this file deals with general interfacing with X and contains the main function and main loop

#define _POSIX_C_SOURCE 200112L
#include <math.h>
#include <unistd.h>
#include <locale.h>
#include <errno.h>
#include <fcntl.h>
#ifdef CATCH_SEGFAULT
# include <signal.h>
# define __USE_GNU
# include <ucontext.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>

#include "common.h"
#include "tty.h"
#include "draw.h"
#include "x.h"
#include "font.h"
#include "buffer.h"
#include "event.h"
#include "settings.h"
#include "icon.h"
//#include "lua.h"

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
	if (width<2) width=2;
	if (height<2) height=2;
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

bool redraw = false;

void force_redraw(void) {
	redraw = true;
}

// todo: clean this up
static void run(void) {
	XMapWindow(W.d, W.win);
	
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
	
	//init_lua();
	//time_log("lua");
	
	Fd xfd = XConnectionNumber(W.d);
	
	while (1) {
		if (tty_read()) {
			redraw = true;
		}
		
		while (XPending(W.d)) {
			XNextEvent(W.d, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			if (HANDLERS[ev.type])
				(HANDLERS[ev.type])(&ev);
		}
		
		if (redraw) {
			redraw = false;
			draw();
		}
		
		tty_wait(xfd, XPending(W.d) ? 0 : 10000);
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

static int gosh_dang_destroy_image_function(XImage* img) {
	return 1;
}

#ifdef CATCH_SEGFAULT
static void hecko(int signum, siginfo_t* si, ucontext_t* context) {
	static unsigned long long n[10];
	print("SEGFAULT CAUGHT!!! probably address: %p\n", (void*)context->uc_mcontext.__gregs[REG_RAX]);
	context->uc_mcontext.__gregs[REG_RAX] = (long long)&n;
}
#endif

int main(int argc, char* argv[argc+1]) {
#ifdef CATCH_SEGFAULT
	signal(SIGSEGV, (__sighandler_t)hecko);
#endif
	if (!(fcntl(2, F_GETFL)!=-1 || errno!=EBADF)) {
		debug_enabled = false;
	}
	
	time_log(NULL);
	
	// hecking locale
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	
	int w = default_width;
	int h = default_height;
	
	W.border = 3;
	
	W.d = XOpenDisplay(NULL);
	if (!W.d)
		die("Could not connect to X server\n");
	W.scr = XDefaultScreen(W.d);
	W.vis = XDefaultVisual(W.d, W.scr);
	if (W.vis->class!=TrueColor)
		die("Cannot handle non true color visual ...\n");

	XrmInitialize();
	XrmDatabase database = XrmGetDatabase(W.d);
	print("database: %p\n", database);
	
	init_atoms();
	
	tty_init(); // todo: maybe try to pass the window size here if we can guess it?
	
	init_term(w, h); // todo: we are going to get a term_resize event quickly after this, mmm.. idk if this is the right place for this, also. I mostly just put it here to simplify the timing logs
	
	time_log("init stuff 1");
	
	XftInit(NULL);
	
	time_log("init xft");
	
	init_fonts(default_font, 0);
	
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
	
	W.gc = XCreateGC(W.d, W.win, GCGraphicsExposures, &(XGCValues){
		.graphics_exposures = False,
	});
	
	// allow listening for window close event
	XSetWMProtocols(W.d, W.win, &W.atoms.wm_delete_window, 1);
	// set _NET_WM_PID property
	XChangeProperty(W.d, W.win, W.atoms.net_wm_pid, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&(pid_t){getpid()}, 1);
	
	// set title
	XSetClassHint(W.d, W.win, &(XClassHint){
		.res_name = "12term",
		.res_class = "12term",
	});
	set_title(NULL);
	
	// set icon
	Pixmap icon_pixmap = XCreatePixmap(W.d, W.win, ICON_SIZE, ICON_SIZE, 24);
	XImage* icon_image = XCreateImage(W.d, W.vis, 24, ZPixmap, 0, (char*)ICON_DATA, ICON_SIZE, ICON_SIZE, 8, 0);
	icon_image->f.destroy_image = gosh_dang_destroy_image_function;
	XPutImage(W.d, icon_pixmap, W.gc, icon_image, 0,0,0,0, icon_image->width, icon_image->height);
	XDestroyImage(icon_image);
	
	XSetWMHints(W.d, W.win, &(XWMHints){
		.flags = InputHint | IconPixmapHint,
		.input = true, // which input focus model
		.icon_pixmap = icon_pixmap,
	});
	
	time_log("created window");
	
	init_input();
	
	time_log("init input");
	
	run();
	return 0;
}

void change_font(const char* name) {
	init_fonts(name, 0);
	change_size(W.w, W.h, true);
}