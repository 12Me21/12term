#define _POSIX_C_SOURCE 200112L
#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <locale.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>

#include "debug.h"
#include "tty.h"
#include "keymap.h"
#include "draw.h"
#include "x.h"
#include "buffer.h"

Xw W = {0};

typedef void (*HandlerFunc)(XEvent*);

#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

static int utf8_encode(Char c, char* out) {
	if (c<0)
		return 0;
	int len=0;
	if (c < 1<<7) {
		out[len++] = c;
	} else if (c < 1<<5+6) {
		out[len++] = 192 | (c>>6 & (1<<5)-1);
		last1:
		out[len++] = 128 | (c & (1<<6)-1);
	} else if (c < 1<<4+6*2) {
		out[len++] = 224 | (c>>6*2 & (1<<4)-1);
		last2:
		out[len++] = 128 | (c>>6 & (1<<6)-1);
		goto last1;
	} else if (c < 1<<3+6*3) {
		out[len++] = 240 | (c>>6*3 & (1<<3)-1);
		goto last2;
	} else { //too big
		return 0;
	}
	return len;
}

void clippaste(void) {
	XConvertSelection(W.d, W.atoms.clipboard, W.atoms.utf8_string, W.atoms.clipboard, W.win, CurrentTime);
}

static bool match_modifiers(int want, int got) {
	if (want==-1)
		return true;
	if (want==-2)
		return (got & ControlMask);
	return want==got;
}

static void on_keypress(XEvent *ev) {
	XKeyEvent *e = &ev->xkey;
	
	//if (IS_SET(MODE_KBDLOCK))
	//	return;
	
	KeySym ksym;
	char buf[64] = {0};
	int len = 0;
	
	Status status;
	if (W.ime.xic)
		len = XmbLookupString(W.ime.xic, e, buf, sizeof buf, &ksym, &status);
	else {
		len = XLookupString(e, buf, sizeof buf, &ksym, NULL); //can't we just use the last argument of this instead of xmb?
		status = XLookupBoth;
	}
	
	if (status==XLookupKeySym || status==XLookupBoth) {
		// look up keysym in the key mapping
		for (KeyMap* map=KEY_MAP; map->output; map++) {
			if (map->k==ksym && match_modifiers(map->modifiers, e->state)) {
				if (map->special==0) {
					tty_write(strlen(map->output), map->output);
				} else {
					int mods = 0;
					if (e->state & ShiftMask)
						mods |= 1;
					if (e->state & Mod1Mask)
						mods |= 2;
					if (e->state & ControlMask)
						mods |= 4;
					if (map->special==1)
						tty_printf(map->output, mods+1);
					else if (map->special==2)
						tty_printf(map->output, mods+1, map->arg);
					else if (map->special==3)
						tty_printf(map->output, map->arg, mods+1);
				}
				goto finish;
			}
		}
	}
	// otherwise, the input is normal text
	if ((status==XLookupChars || status==XLookupBoth) && len>0) {
		// idk how good this is...
		if (len == 1 && e->state & Mod1Mask) {
			//if (IS_SET(MODE_8BIT)) {
			//	if (*buf < 0177) {
			//		Rune c = *buf | 0x80;
			//		len = utf8encode(c, buf);
			//	}
			//} else {
			buf[1] = buf[0];
			buf[0] = '\033';
			len = 2;
		}
		tty_write(len, buf);
	}
 finish:;
}

static void on_expose(XEvent* e) {
	(void)e;
	repaint();
}

static void init_pixmap(void) {
	if (W.pix)
		XFreePixmap(W.d, W.pix);
	W.pix = XCreatePixmap(W.d, W.win, W.w, W.h, DefaultDepth(W.d, W.scr));
	if (W.draw) {
		XftDrawChange(W.draw, W.pix);
	} else {
		W.draw = XftDrawCreate(W.d, W.pix, W.vis, W.cmap);
	}
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
}

// when the size of the character cells changes (i.e. when changing fontsize)
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
	
	if (hangup)
		tty_hangup();
	
	for (int i=0; i<4; i++) {
		FcPatternDestroy(W.fonts[i].pattern);
		XftFontClose(W.d, W.fonts[i].match);
	}
	
	//if (W.draw)
		//	XftDrawDestroy(W.draw);
	
	draw_free();
	//FcFini(); // aaa
	
	XCloseDisplay(W.d); // why does this hang sometimes
	
	_exit(0); //is this right??
}

static void on_clientmessage(XEvent* e) {
	if (e->xclient.message_type == W.atoms.xembed && e->xclient.format == 32) {
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

static void on_focusin(XEvent* e) {
	if (e->xfocus.mode == NotifyGrab)
		return;
	
	if (W.ime.xic)
		XSetICFocus(W.ime.xic);
}

static void on_focusout(XEvent* e) {
	if (e->xfocus.mode == NotifyGrab)
		return;
	
	if (W.ime.xic)
		XUnsetICFocus(W.ime.xic);
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
	[KeyPress] = on_keypress,
	[Expose] = on_expose,
	[ConfigureNotify] = on_configurenotify,
	[FocusIn] = on_focusin,
	[FocusOut] = on_focusout,
	[SelectionNotify] = on_selectionnotify,
};

void clipboard_copy() {
	
}

static int max(int a, int b) {
	if (a>b)
		return a;
	return b;
}

static int ceildiv(int a, int b) {
	return (a+b-1)/b;
}

static int load_font(Font* f, FcPattern* pattern) {
	FcPattern* configured = FcPatternDuplicate(pattern);
	if (!configured)
		return 1;
	
	FcConfigSubstitute(NULL, configured, FcMatchPattern);
	XftDefaultSubstitute(W.d, W.scr, configured);
	
	FcResult result;
	FcPattern* match = FcFontMatch(NULL, configured, &result);
	if (!match) {
		FcPatternDestroy(configured);
		return 1;
	}
	
	f->match = XftFontOpenPattern(W.d, match);
	if (!f->match) {
		FcPatternDestroy(configured);
		FcPatternDestroy(match);
		return 1;
	}
	
	int wantattr, haveattr;
	
	// check slant/weight to see if
	if (XftPatternGetInteger(pattern, "slant", 0, &wantattr) == XftResultMatch) {
		if (XftPatternGetInteger(f->match->pattern, "slant", 0, &haveattr)!=XftResultMatch || haveattr<wantattr) {
			f->badslant = true;
		}
	}
	if (XftPatternGetInteger(pattern, "weight", 0, &wantattr) == XftResultMatch) {
		if (XftPatternGetInteger(f->match->pattern, "weight", 0, &haveattr)!=XftResultMatch || haveattr != wantattr) {
			f->badweight = true;
		}
	}
	
	const char ascii_printable[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
	int len = strlen(ascii_printable);
	XGlyphInfo extents;
	XftTextExtentsUtf8(W.d, f->match, (const FcChar8*)ascii_printable, len, &extents);
	f->set = NULL;
	f->pattern = configured;
	f->ascent = f->match->ascent;
	f->descent = f->match->descent;
	f->lbearing = 0;
	f->rbearing = f->match->max_advance_width;
	f->height = f->ascent + f->descent;
	f->width = ceildiv(extents.xOff, len);
	
	return 0;
}

static void init_fonts(const char* fontstr, double fontsize) {
	FcPattern* pattern;
		
	if (fontstr[0] == '-')
		pattern = XftXlfdParse(fontstr, False, False);
	else
		pattern = FcNameParse((const FcChar8*)fontstr);
	
	//if (!pattern)
	//	die("can't open font %s\n", fontstr);
	
	
	if (fontsize) {
		FcPatternDel(pattern, FC_PIXEL_SIZE);
		FcPatternDel(pattern, FC_SIZE);
		FcPatternAddDouble(pattern, FC_PIXEL_SIZE, fontsize);
	} else {
		if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &fontsize) == FcResultMatch) {
			//
		} else if (FcPatternGetDouble(pattern, FC_SIZE, 0, &fontsize) == FcResultMatch) {
			//
		} else {
			FcPatternAddDouble(pattern, FC_PIXEL_SIZE, 12);
			fontsize = 12;
		}
	}
	
	time_log("parsed font pattern");
	
	load_font(&W.fonts[0], pattern);
	
	time_log("loaded font 0");
	
	// italic
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
	load_font(&W.fonts[2], pattern);
	time_log("loaded font 2");
	
	// bold+italic
	FcPatternDel(pattern, FC_WEIGHT);
	FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
	load_font(&W.fonts[3], pattern);
	time_log("loaded font 3");
	// bold
	FcPatternDel(pattern, FC_SLANT);
	FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
	load_font(&W.fonts[1], pattern);
	time_log("loaded font 1");
	
	FcPatternDestroy(pattern);
	
}

void xim_spot(int x, int y) {
	if (!W.ime.xic)
		return;
	
	W.ime.spot = (XPoint){
		.x = W.border + x * W.cw,
		.y = W.border + (y+1) * W.ch,
	};
	XSetICValues(W.ime.xic, XNPreeditAttributes, W.ime.spotlist, NULL);
}

static void ximinstantiate(Display* d, XPointer client, XPointer call);

static void ximdestroy(XIM xim, XPointer client, XPointer call) {
	W.ime.xim = NULL;
	XRegisterIMInstantiateCallback(W.d, NULL, NULL, NULL, ximinstantiate, NULL);
	XFree(W.ime.spotlist);
}

static int xicdestroy(XIC xim, XPointer client, XPointer call) {
	W.ime.xic = NULL;
	return 1;
}

static int ximopen(Display* d) {
	W.ime.xim = XOpenIM(d, NULL, NULL, NULL);
	if (W.ime.xim == NULL)
		return 0;
	
	if (XSetIMValues(W.ime.xim, XNDestroyCallback, &(XIMCallback){.callback = ximdestroy}, NULL))
		print("XSetIMValues: Could not set XNDestroyCallback.\n");
	
	W.ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &W.ime.spot, NULL);
	
	if (W.ime.xic == NULL) {
		W.ime.xic = XCreateIC(W.ime.xim, XNInputStyle,
			XIMPreeditNothing | XIMStatusNothing,
			XNClientWindow, W.win,
			XNDestroyCallback, &(XICCallback){.callback = xicdestroy},
			NULL);
	}
	if (W.ime.xic == NULL)
		print("XCreateIC: Could not create input context.\n");
	
	return 1;
}

static void ximinstantiate(Display* d, XPointer client, XPointer call) {
	if (ximopen(d))
		XUnregisterIMInstantiateCallback(d, NULL, NULL, NULL, ximinstantiate, NULL);
}

static void init_xim(void) {
	if (!ximopen(W.d)) {
		XRegisterIMInstantiateCallback(W.d, NULL, NULL, NULL, ximinstantiate, NULL);
	}
}

static int timediff(struct timespec t1, struct timespec t2) {
	return (t1.tv_sec-t2.tv_sec)*1000 + (t1.tv_nsec-t2.tv_nsec)/1E6;
}

static double minlatency = 8;
static double maxlatency = 33;

int ttyfd = 0;

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
	struct timespec seltv, *tv, now, trigger;
	bool drawing = false;
	bool got_text = false;
	bool got_draw = false;
	bool readed = false;
	
	while (1) {
		int xfd = XConnectionNumber(W.d);
		
		fd_set rfd;
		FD_ZERO(&rfd);
		FD_SET(ttyfd, &rfd);
		FD_SET(xfd, &rfd);
		
		if (XPending(W.d))
			timeout = 0;
		
		seltv.tv_sec = timeout / 1000;
		seltv.tv_nsec = 1000000 * (timeout % 1000);
		tv = timeout>=0 ? &seltv : NULL;
		
		if (pselect(max(xfd, ttyfd)+1, &rfd, NULL, NULL, tv, NULL) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		clock_gettime(CLOCK_MONOTONIC, &now);
		
		
		if (FD_ISSET(ttyfd, &rfd)) {
			if (!got_text) {
				time_log("first text");
				got_text = true;
			}
			tty_read();
			readed = true;
		}
		
		int xev = 0;
		while (XPending(W.d)) {
			xev = 1;
			XNextEvent(W.d, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			if (HANDLERS[ev.type])
				(HANDLERS[ev.type])(&ev);
		}
		
		// idea: instead of using just a timeout
		// we can be more intelligent about this.
		// detect newlines etc.
		if (FD_ISSET(ttyfd, &rfd) || xev) {
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

int main(int argc, char* argv[argc+1]) {
	time_log(NULL);
	
	// hecking locale
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	
	// default size
	int w = 50;
	int h = 10;
	
	W.border = 3;
	
	W.d = XOpenDisplay(NULL);
	W.scr = XDefaultScreen(W.d);
	W.vis = XDefaultVisual(W.d, W.scr);
	
	time_log("x stuff 1");
	
	ttyfd = tty_new(); // todo: maybe try to pass the window size here if we can guess it?
	
	FcInit();
	// todo: this
	init_fonts("cascadia code,monospace:pixelsize=16:antialias=true:autohint=true", 0);
	
	int cw = ceil(W.fonts[0].width);
	int ch = ceil(W.fonts[0].height);
	
	W.w = cw*w+W.border*2;
	W.h = ch*h+W.border*2;
	
	W.cmap = XDefaultColormap(W.d, W.scr);
	
	Window parent = XRootWindow(W.d, W.scr);
	
	W.attrs = (XSetWindowAttributes){
		//.background_pixel = TODO,
		//.border_pixel = TODO,
		.bit_gravity = NorthWestGravity,
		.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask | ExposureMask | VisibilityChangeMask | StructureNotifyMask | ButtonMotionMask | ButtonPressMask | ButtonReleaseMask,
		.colormap = W.cmap,
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
	
	init_xim();
	
	init_atoms();
	
	XSetWMProtocols(W.d, W.win, &W.atoms.wm_delete_window, 1);
	pid_t thispid = getpid();
	XChangeProperty(W.d, W.win, W.atoms.net_wm_pid, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&thispid, 1);
	
	XMapWindow(W.d, W.win);
	//XSync(W.d, False); //do we need this?
	
	time_log("created window");
	
	update_charsize(cw, ch);
	
	init_term(w, h);
	
	run();
	return 0;
}
