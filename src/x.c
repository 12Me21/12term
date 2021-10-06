// this file deals with general interfacing with X and contains the main function and main loop

#define _POSIX_C_SOURCE 200112L
#include <math.h>
#include <unistd.h>
#include <locale.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#ifdef CATCH_SEGFAULT
# include <signal.h>
# define __USE_GNU
# include <ucontext.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include <xcb/render.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_icccm.h>

#include "xft/Xft.h"

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
bool parse_x_color(const utf8* c, RGBColor* out) {
	XColor ret;
	if (XParseColor(W.d, W.cmap, c, &ret)) {
		*out = (RGBColor){
			ret.red*255/65535,
			ret.green*255/65535,
			ret.blue*255/65535,
		};
		return true;
	}
	return false;
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
void change_size(Px w, Px h, bool charsize, bool resize) {
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
		if (resize)
			XResizeWindow(W.d, W.win, W.w, W.h);
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

static bool redraw = false;

void force_redraw(void) {
	redraw = true;
}

static Nanosec timediff(struct timespec t1, struct timespec t2) {
	return (t1.tv_sec-t2.tv_sec)*1000L*1000*1000 + (t1.tv_nsec-t2.tv_nsec);
}

static Nanosec min_redraw = 10*1000*1000;

// todo: clean this up
static void run(void) {
	XMapWindow(W.d, W.win); // why doesn't xcb_map_window work here?
	
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
	
	change_size(w, h, true, false);
	
	time_log("window mapped");
	
	//init_lua();
	//time_log("lua");
	
	Fd xfd = xcb_get_file_descriptor(W.c);
	
	struct timespec last_redraw = {0};
	
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
		
		Nanosec timeout = 10000L*1000*1000;
		
		if (redraw) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			Nanosec since_last = timediff(now, last_redraw);
			//print("since last: %lld", since_last/1000/1000);
			if (since_last>=min_redraw) {
				draw(false);
				redraw = false;
				last_redraw = now;
			} else {
				timeout = min_redraw - since_last + 1000;
				//print("delaying redraw for %lld ms\n", timeout/1000/1000);
			}
		}
		
		tty_wait(xfd, XPending(W.d) ? 0 : timeout);
	}
}

static void init_atoms(void) {
	utf8* ATOM_NAMES[] = {
		"_XEMBED", "WM_DELETE_WINDOW", "_NET_WM_NAME", "_NET_WM_ICON_NAME", "_NET_WM_PID", "UTF8_STRING", "CLIPBOARD", "INCR", "TARGETS",
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

void set_title(utf8* s) {
	if (!s)
		s = "12term"; // default title
	XSetWMName(W.d, W.win, &(XTextProperty){
			(void*)s, W.atoms.utf8_string, 8, strlen(s)
	});
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
	time_log(NULL);
	
	// hecking locale
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers(""); // is this required
	
	int w = settings.width;
	int h = settings.height;
	
	W.border = 3;
	
	//
	W.d = XOpenDisplay(NULL);
	if (!W.d)
		die("Could not connect to X server\n");
	W.c = XGetXCBConnection(W.d);
	W.scr = XDefaultScreen(W.d);
	// when ready, replace above code with:
	/* W.c = xcb_connect(NULL, &W.scr); */
	
	W.scr2 = xcb_aux_get_screen(W.c, W.scr);
	W.depth = xcb_aux_get_depth(W.c, W.scr2);
	W.vis = XDefaultVisual(W.d, W.scr);
	// check if user has modern display (otherwise nnnnnnnn sorry i dont want to deal with this)
	if (W.vis->class!=TrueColor)
		die("Cannot handle non truecolor visual ...\n");
	
	xcb_render_query_version_cookie_t c = xcb_render_query_version_unchecked(W.c, XCB_RENDER_MAJOR_VERSION, XCB_RENDER_MINOR_VERSION);
	xcb_render_query_version_reply_t* r = xcb_render_query_version_reply(W.c, c, NULL);
	print("xrender version: %d.%d\n", r->major_version, r->minor_version);
	free(r);
	
	W.format = (xcb_render_pictforminfo_t*)XRenderFindVisualFormat(W.d, W.vis);
	if (!W.format)
		die("cant find visual format ...\n");
	
	W.cmap = XDefaultColormap(W.d, W.scr);
	
	// init db
	XrmInitialize();
	load_settings(&argc, argv);
	
	init_atoms();
	
	tty_init(); // todo: maybe try to pass the window size here if we can guess it?
	
	init_term(w, h); // todo: we are going to get a term_resize event quickly after this, mmm.. idk if this is the right place for this, also. I mostly just put it here to simplify the timing logs
	
	time_log("init stuff 1");
	
	if (!FcInit()) // you can skip this but it'll just init itself when the first font is loaded anyway
		die("fontconfig init failed");
	time_log("init fontconfig");
	
	XftDisplayInfoInit();
	time_log("init xft");
	
	if (FT_Init_FreeType(&_XftFTlibrary))
		die("freetype init failed");
	time_log("init freetype");
		
	load_fonts(settings.faceName, settings.faceSize);
	
	// messy messy
	W.w = W.cw*w+W.border*2;
	W.h = W.ch*h+W.border*2;
	
	// create the window
	
	unsigned long bg_pixel = alloc_color((Color){.truecolor=true,.rgb=settings.background}); // yuck
	
	W.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask | ExposureMask | VisibilityChangeMask | StructureNotifyMask | ButtonMotionMask | ButtonPressMask | ButtonReleaseMask;
	
	W.win = xcb_generate_id(W.c);
	xcb_aux_create_window(W.c, W.depth,
		W.win, W.scr2->root,
		0, 0, W.w, W.h,
		0, 
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		W.vis->visualid,
		XCB_CW_BACK_PIXEL | XCB_CW_BIT_GRAVITY | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP,
		&(xcb_params_cw_t) {
			.back_pixel = bg_pixel,
			.bit_gravity = XCB_GRAVITY_NORTH_WEST,
			.event_mask = W.event_mask,
			.colormap = W.cmap,
		}
	);
	// set _NET_WM_PID property
	XChangeProperty(W.d, W.win, W.atoms.net_wm_pid, XA_CARDINAL, 32, PropModeReplace, (void*)&(pid_t){getpid()}, 1);
	
	W.gc = xcb_generate_id(W.c);
	xcb_create_gc(W.c, W.gc, W.win, XCB_GC_GRAPHICS_EXPOSURES, &(xcb_params_gc_t){
		.graphics_exposures = false,
	});
	
	// allow listening for window close event
	// todo: for xcb there's a special way to handle atoms
	XSetWMProtocols(W.d, W.win, &W.atoms.wm_delete_window, 1);
	
	// set title
	XSetClassHint(W.d, W.win, &(XClassHint){
		.res_name = "12term",
		.res_class = "12term",
	});
	set_title(NULL);
	
	// set icon
	{
		xcb_pixmap_t pixmap = xcb_generate_id(W.c);
		xcb_create_pixmap(W.c, 24, pixmap, W.win, ICON_SIZE, ICON_SIZE);
		xcb_put_image(W.c, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, W.gc, ICON_SIZE, ICON_SIZE, 0, 0, 0, 24, ICON_SIZE*ICON_SIZE*4, ICON_DATA);
		xcb_icccm_set_wm_hints(W.c, W.win, &(xcb_icccm_wm_hints_t){
			.flags = XCB_ICCCM_WM_HINT_INPUT | XCB_ICCCM_WM_HINT_ICON_PIXMAP,
			.input = true,
			.icon_pixmap = pixmap,
		});
	}

	time_log("created window");
	
	//init_input();
	
	time_log("init input");
	
	run();
	return 0;
}

void change_font(const utf8* name) {
	load_fonts(name, settings.faceSize);
	int w = W.cw*T.width+W.border*2;
	int h = W.ch*T.height+W.border*2;
	change_size(w, h, true, true);
}
