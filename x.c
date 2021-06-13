#define _POSIX_C_SOURCE 200112L
#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <locale.h>
#include <errno.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>

#include "common.h"
#include "tty.h"
#include "draw.h"
#include "x.h"
#include "buffer.h"
#include "input.h"
#include "font.h"

Xw W = {0};

typedef void (*HandlerFunc)(XEvent*);

#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

void clippaste(void) {
	XConvertSelection(W.d, W.atoms.clipboard, W.atoms.utf8_string, W.atoms.clipboard, W.win, CurrentTime);
}

void on_visibilitynotify(XEvent *ev) {
	XVisibilityEvent* e = &ev->xvisibility;
	print("visibility\n");
	//MODBIT(win.mode, e->state != VisibilityFullyObscured, MODE_VISIBLE);
}

static void on_expose(XEvent* e) {
	(void)e;
	print("expose\n");
	dirty_all();
	draw(); // maybe this helps
	//repaint();
}

static void init_pixmap(void) {
	if (W.pix)
		XFreePixmap(W.d, W.pix);
	W.pix = XCreatePixmap(W.d, W.win, W.w, W.h, DefaultDepth(W.d, W.scr));
	if (W.draw)
		XftDrawChange(W.draw, W.pix);
	else
		W.draw = XftDrawCreate(W.d, W.pix, W.vis, W.cmap);
	
	clear_background();
}

// when the size of the terminal (in character cells) changes
// (also called to initialize size)
// todo: what if the size (in pixels) changes but not the size in char cells?
// like, if someone resizes the window by 1px, OR if the font size changes?
static void update_size(int width, int height) {
	W.w = W.border*2+width*W.cw;
	W.h = W.border*2+height*W.ch;
	tty_resize(width, height, width*W.cw, height*W.ch);
	init_pixmap();
	term_resize(width, height);
	draw_resize(width, height); //todo: order?
	//draw();
}

// when the size of the character cells changes (i.e. when changing fontsize)
// we don't actually use this yet except during init
static void update_charsize(Px w, Px h) {
	W.cw = w;
	W.ch = h;
	if (W.under_cursor)
		XFreePixmap(W.d, W.under_cursor);
	W.under_cursor = XCreatePixmap(W.d, W.win, W.cw*2, W.ch, DefaultDepth(W.d, W.scr));
	
	Px base = W.border*2;
	XSetWMProperties(W.d, W.win, NULL, NULL, NULL, 0, &(XSizeHints){
		.flags = PSize | PResizeInc | PBaseSize | PMinSize,
		.width = W.w,
		.height = W.h,
		.width_inc = W.cw,
		.height_inc = W.ch,
		.base_width = base,
		.base_height = base,
		.min_width = base + W.cw*2,
		.min_height = base + W.ch*2,
	}, &(XWMHints){
		.flags = InputHint, .input = 1
	}, NULL); //todo: class hint?
	
}

static void on_configurenotify(XEvent* e) {
	Px width = e->xconfigure.width;
	Px height = e->xconfigure.height;
	if (width==W.w && height==W.h)
		return;
	
	update_size((width-W.border*2)/W.cw, (height-W.border*2)/W.ch);
}

void sleep_forever(bool hangup) {
	print("goodnight...\n");
	
	//if (hangup)
	tty_hangup();
	
	fonts_free();
	
	//if (W.draw)
		//	XftDrawDestroy(W.draw);
	
	draw_free();
	//FcFini(); // aaa
	
	XCloseDisplay(W.d);
	
	_exit(0); //is this right?
}

static void on_clientmessage(XEvent* e) {
	if (e->xclient.message_type == W.atoms.xembed && e->xclient.format == 32) {
		// do we need to do this anymore?
		if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
			//win.mode |= MODE_FOCUSED;
			//xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			//win.mode &= ~MODE_FOCUSED;
		}
	} else if (e->xclient.data.l[0] == W.atoms.wm_delete_window) {
		print("window closing\n");
		sleep_forever(true);
	}
}

// where does this go
void tty_paste_text(int len, const char text[len]) {
	if (T.bracketed_paste)
		tty_write(6, "\x1B[200~");
	tty_write(len, text);
	if (T.bracketed_paste)
		tty_write(6, "\x1B[201~");
}

void on_selectionnotify(XEvent* e) {
	Atom property = None;
	if (e->type == SelectionNotify)
		property = e->xselection.property;
	else if (e->type == PropertyNotify)
		property = e->xproperty.atom;
	
	if (property == None)
		return;
	
	unsigned long ofs = 0;
	unsigned long rem;
	do {
		unsigned long nitems;
		int format;
		unsigned char* data;
		Atom type;
		if (XGetWindowProperty(W.d, W.win, property, ofs, BUFSIZ/4, False, AnyPropertyType, &type, &format, &nitems, &rem, &data)) {
			print("Clipboard allocation failed\n");
			return;
		}
		
		if (e->type == PropertyNotify && nitems == 0 && rem == 0) {
			/*
			 * If there is some PropertyNotify with no data, then
			 * this is the signal of the selection owner that all
			 * data has been transferred. We won't need to receive
			 * PropertyNotify events anymore.
			 */
			W.attrs.event_mask &= ~PropertyChangeMask;
			XChangeWindowAttributes(W.d, W.win, CWEventMask, &W.attrs);
		}
		
		if (type == W.atoms.incr) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			W.attrs.event_mask |= PropertyChangeMask;
			XChangeWindowAttributes(W.d, W.win, CWEventMask, &W.attrs);
			
			/*
			 * Deleting the property is the transfer start signal.
			 */
			XDeleteProperty(W.d, W.win, property);
			continue;
		}
		
		// replace \n with \r
		unsigned char* repl = data;
		unsigned char* last = data + nitems*format/8;
		while ((repl = memchr(repl, '\n', last - repl))) {
			*repl++ = '\r';
		}
		
		tty_paste_text(nitems*format/8, (char*)data);
		
		XFree(data);
		/* number of 32-bit chunks returned */
		ofs += nitems * format/32;
	} while (rem > 0);
	
	/*
	 * Deleting the property again tells the selection owner to send the
	 * next data chunk in the property.
	 */
	XDeleteProperty(W.d, W.win, property);
}

static const HandlerFunc HANDLERS[LASTEvent] = {
	[ClientMessage] = on_clientmessage,
	[Expose] = on_expose,
	//[VisibilityNotify] = on_visibilitynotify,
	[ConfigureNotify] = on_configurenotify,
	[SelectionNotify] = on_selectionnotify,
	// in input.c:
	[KeyPress] = on_keypress,
	[FocusIn] = on_focusin,
	[FocusOut] = on_focusout,
};

void clipboard_copy() {
	
}

static int timediff(struct timespec t1, struct timespec t2) {
	return (t1.tv_sec-t2.tv_sec)*1000 + (t1.tv_nsec-t2.tv_nsec)/1E6;
}

static double minlatency = 8;
static double maxlatency = 33;

Fd ttyfd = 0;

static int max(int a, int b) {
	if (a>b)
		return a;
	return b;
}

static bool wait_until(Fd xfd, Fd ttyfd, int timeout) {
	fd_set rfd;
	while (1) {
		FD_ZERO(&rfd);
		FD_SET(ttyfd, &rfd);
		FD_SET(xfd, &rfd);
	
		if (XPending(W.d))
			timeout = 0;
	
		struct timespec seltv = {
			.tv_sec = timeout / 1000,
			.tv_nsec = 1000000 * (timeout % 1000),
		};
		struct timespec* tv = timeout>=0 ? &seltv : NULL;
	
		if (pselect(max(xfd, ttyfd)+1, &rfd, NULL, NULL, tv, NULL) < 0) {
			if (errno != EINTR)
				die("select failed: %s\n", strerror(errno));
		} else
			break;
	}
	return FD_ISSET(ttyfd, &rfd);
}

static void run(void) {
	XEvent ev;
	do {
		XNextEvent(W.d, &ev);
		if (XFilterEvent(&ev, None))
			continue;
		if (ev.type == ConfigureNotify) {
			W.w = ev.xconfigure.width;
			W.h = ev.xconfigure.height;
		}
	} while (ev.type != MapNotify);
	
	time_log("window mapped");
	
	int w = (W.w-W.border*2) / W.cw;
	int h = (W.h-W.border*2) / W.ch;
	
	update_size(w, h); // must be called after tty is created
	
	int timeout = -1;
	struct timespec now, trigger;
	bool drawing = false;
	bool got_text = false;
	bool got_draw = false;
	bool readed = false;
	
	while (1) {
		Fd xfd = XConnectionNumber(W.d);
		
		bool text = wait_until(xfd, ttyfd, timeout);
		
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
		
		// idea: instead of using just a timeout
		// we can be more intelligent about this.
		// detect newlines etc.
		if (text || xev) {
			if (!drawing) {
				trigger = now;
				drawing = true;
			}
			timeout = (maxlatency - timediff(now, trigger)) / maxlatency * minlatency;
			if (timeout > 0)
				continue; /* we have time, try to find idle */
		}
		
		timeout = -1;
		
		if (readed) {
			if (!got_draw) {
				time_log("first draw");
				got_draw = true;
			}
			
			draw();
			xim_spot(T.c.x, T.c.y);
			readed = false;
			drawing = false;
		}
		
		XFlush(W.d);
	}
}

static void init_atoms(void) {
	W.atoms.xembed = XInternAtom(W.d, "_XEMBED", False);
	W.atoms.wm_delete_window = XInternAtom(W.d, "WM_DELETE_WINDOW", False);
	W.atoms.net_wm_name = XInternAtom(W.d, "_NET_WM_NAME", False);
	W.atoms.net_wm_icon_name = XInternAtom(W.d, "_NET_WM_ICON_NAME", False);
	W.atoms.net_wm_pid = XInternAtom(W.d, "_NET_WM_PID", False);
	W.atoms.utf8_string = XInternAtom(W.d, "UTF8_STRING", False);
	if (W.atoms.utf8_string == None)
		W.atoms.utf8_string = XA_STRING;
	W.atoms.clipboard = XInternAtom(W.d, "CLIPBOARD", False);
	W.atoms.incr = XInternAtom(W.d, "INCR", False);
}

extern RGBColor default_background; //messy messy messy

int main(int argc, char* argv[argc+1]) {
	time_log(NULL);
	
	// hecking locale
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	
	// default size
	int w = 50;
	int h = 10;
	
	W.border = 3;
	
	W.ligatures = true;
	
	W.d = XOpenDisplay(NULL);
	W.scr = XDefaultScreen(W.d);
	W.vis = XDefaultVisual(W.d, W.scr);
	
	time_log("x stuff 1");
	
	ttyfd = tty_new(); // todo: maybe try to pass the window size here if we can guess it?
	
	FcInit();
	// todo: this
	init_fonts("cascadia code,fira code,monospace:pixelsize=16:antialias=true:autohint=true", 0);
	
	// messy messy
	W.w = W.cw*w+W.border*2;
	W.h = W.ch*h+W.border*2;
	
	W.cmap = XDefaultColormap(W.d, W.scr);
	
	Window parent = XRootWindow(W.d, W.scr);
	
	long unsigned int bg_pixel = make_color((Color){.truecolor=true,.rgb=default_background}).pixel;
	
	W.attrs = (XSetWindowAttributes){
		.bit_gravity = NorthWestGravity,
		.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask | ExposureMask | VisibilityChangeMask | StructureNotifyMask | ButtonMotionMask | ButtonPressMask | ButtonReleaseMask,
		.colormap = W.cmap,
		.background_pixel = bg_pixel,
		.border_pixel = bg_pixel,
	};
	W.win = XCreateWindow(W.d, parent,
		0, 0, W.w, W.h, // geometry
		0, // border width
		XDefaultDepth(W.d, W.scr), // depth
		InputOutput, // class
		W.vis, // visual
		CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap, // value mask
		&W.attrs
	);
	W.gc = XCreateGC(W.d, parent, GCGraphicsExposures, &(XGCValues){
		.graphics_exposures = False,	
	});
	
	init_input();
	
	init_atoms();
	
	XSetWMProtocols(W.d, W.win, &W.atoms.wm_delete_window, 1);
	pid_t thispid = getpid();
	XChangeProperty(W.d, W.win, W.atoms.net_wm_pid, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&thispid, 1);
	
	update_charsize(W.cw, W.ch);
	
	XMapWindow(W.d, W.win);
	//XSync(W.d, False); //do we need this?
	
	time_log("created window");
	
	init_term(w, h);
	
	run();
	return 0;
}
