#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <locale.h>
#include <errno.h>
#include <time.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#define Cursor Cursor_

#include "debug.h"
#include "tty.h"
#include "buffer.h"
#include "keymap.h"

typedef int Px;

/* Font structure */
#define Font Font_
typedef struct {
	int height;
	int width;
	int ascent;
	int descent;
	bool badslant;
	bool badweight;
	short lbearing;
	short rbearing;
	XftFont* match;
	FcFontSet* set;
	FcPattern* pattern;
} Font;

typedef struct Xw {
	Display* d;
	int scr;
	Visual* vis;
	Colormap cmap;
	Window win;
	Pixmap pix;
	
	Pixmap cursor;
	bool cursor_drawn;
	int cursor_x, cursor_y;
	
	GC gc;
	Px w,h;
	Px cw,ch;
	Px border;
	float cwscale, chscale;// todo
	XftDraw* draw;
	
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

Xw W;

Term T;

typedef struct {
	XftFont *font;
	int flags;
	Char unicodep;
} Fontcache;

static Fontcache* frc = NULL;
static int frclen = 0;
static int frccap = 0;

typedef void (*HandlerFunc)(XEvent*);

#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

static void on_clientmessage(XEvent* e) {
	if (e->xclient.message_type == W.atoms.xembed && e->xclient.format == 32) {
		if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
			//win.mode |= MODE_FOCUSED;
			//xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			//win.mode &= ~MODE_FOCUSED;
		}
	} else if (e->xclient.data.l[0] == W.atoms.wm_delete_window) {
		tty_hangup();
		exit(0);
	}
}

int utf8_encode(Char c, char* out) {
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

bool match_modifiers(int want, int got) {
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
	const char* out = buf;
	int len = 0;
	Status status;
	
	if (W.ime.xic)
		len = XmbLookupString(W.ime.xic, e, buf, sizeof buf, &ksym, &status);
	else
		len = XLookupString(e, buf, sizeof buf, &ksym, NULL);
	
	for (KeyMap* map=KEY_MAP; map->output; map++) {
		if (map->k==ksym && match_modifiers(map->modifiers, e->state)) {
			out = map->output;
			if (map->special==0) {
				len = strlen(out);
			} else {
				int mods = 0;
				if (e->state & ShiftMask)
					mods |= 1;
				if (e->state & Mod1Mask)
					mods |= 2;
				if (e->state & ControlMask)
					mods |= 4;
				if (map->special==1)
					len = snprintf(buf, sizeof(buf), map->output, mods+1);
				else if (map->special==2)
					len = snprintf(buf, sizeof(buf), map->output, mods+1, map->arg);
				else if (map->special==3)
					len = snprintf(buf, sizeof(buf), map->output, map->arg, mods+1);
				out = buf;
			}
			goto found;
		}
	}
	
	if (len == 0)
		return;
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
	
 found:
	//print("got keypresses: [%d] %s\n", ksym, out);
	tty_write(&T, len, out);
}

void repaint(void) {
	XCopyArea(W.d, W.pix, W.win, W.gc, 0, 0, W.w, W.h, 0, 0);
}

void on_expose(XEvent* e) {
	(void)e;
	repaint();
}

static void init_pixmap(void) {
	if (W.pix)
		XFreePixmap(W.d, W.pix);
	W.pix = XCreatePixmap(W.d, W.win, W.w, W.h, DefaultDepth(W.d, W.scr));
	//XSetForeground(W.dpy, W.gc, T.palette[defaultbg].pixel);
	//XFillRectangle(xw.dpy, xw.buf, dc.gc, 0, 0, win.w, win.h);
	
	
	if (W.draw) {
		XftDrawChange(W.draw, W.pix);
	} else {
		W.draw = XftDrawCreate(W.d, W.pix, W.vis, W.cmap);
	}
}

// (size in characters)
void update_size(int width, int height) {
	W.w = W.border*2+width*W.cw;
	W.h = W.border*2+height*W.ch;
	init_pixmap();
	term_resize(&T, width, height);
	tty_resize(&T, width*W.cw, height*W.ch);
}

// when the size of the character cells changes (i.e. when changing fontsize)
void update_charsize(Px w, Px h) {
	W.cw = w;
	W.ch = h;
	if (W.cursor)
		XFreePixmap(W.d, W.cursor);
	W.cursor = XCreatePixmap(W.d, W.win, W.cw, W.ch, DefaultDepth(W.d, W.scr));
}

void on_configurenotify(XEvent* e) {
	Px width = e->xconfigure.width;
	Px height = e->xconfigure.height;
	if (width==W.w && height==W.h)
		return;
	
	update_size((width-W.border*2)/W.cw, (height-W.border*2)/W.ch);
}

static HandlerFunc handler[LASTEvent] = {
	[ClientMessage] = on_clientmessage,
	[KeyPress] = on_keypress,
	[Expose] = on_expose,
	[ConfigureNotify] = on_configurenotify,
};

int max(int a, int b) {
	if (a>b)
		return a;
	return b;
}

static int timediff(struct timespec t1, struct timespec t2) {
	return (t1.tv_sec-t2.tv_sec)*1000 + (t1.tv_nsec-t2.tv_nsec)/1E6;
}

static double minlatency = 8;
static double maxlatency = 33;

static void draw(void) {
	draw_screen(&T);
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
	
	int ttyfd = tty_new(NULL);
	
	time_log("created tty");
	
	int w = (W.w-W.border*2) / W.cw;
	int h = (W.h-W.border*2) / W.ch;
	
	update_size(w, h); // gehhhh
	
	int timeout = -1;
	struct timespec seltv, *tv, now, trigger;
	int drawing = 0;
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
		
		if (FD_ISSET(ttyfd, &rfd))
			ttyread(&T);
		
		int xev = 0;
		while (XPending(W.d)) {
			xev = 1;
			XNextEvent(W.d, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			if (handler[ev.type])
				(handler[ev.type])(&ev);
		}
		
		
		// idea: instead of using just a timeout
		// we can be more intelligent about this.
		// detect newlines etc.
		if (FD_ISSET(ttyfd, &rfd) || xev) {
			if (!drawing) {
				trigger = now;
				drawing = 1;
			}
			timeout = (maxlatency - timediff(now, trigger)) / maxlatency * minlatency;
			if (timeout > 0)
				continue; /* we have time, try to find idle */
		}
		
		timeout = -1;
		
		draw();
		XFlush(W.d);
		drawing = 0;
	}
}

static void init_hints(void) {
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

void ximinstantiate(Display* d, XPointer client, XPointer call);

void ximdestroy(XIM xim, XPointer client, XPointer call) {
	W.ime.xim = NULL;
	XRegisterIMInstantiateCallback(W.d, NULL, NULL, NULL, ximinstantiate, NULL);
	XFree(W.ime.spotlist);
}

int xicdestroy(XIC xim, XPointer client, XPointer call) {
	W.ime.xic = NULL;
	return 1;
}

int ximopen(Display* d) {
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

void ximinstantiate(Display* d, XPointer client, XPointer call) {
	if (ximopen(d))
		XUnregisterIMInstantiateCallback(d, NULL, NULL, NULL, ximinstantiate, NULL);
}

static void init_xim(void) {
	if (!ximopen(W.d)) {
		XRegisterIMInstantiateCallback(W.d, NULL, NULL, NULL, ximinstantiate, NULL);
	}
}

int main(int argc, char* argv[argc+1]) {
	time_log("");
	
	// hecking locale
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	
	// default size
	int w = 50;
	int h = 10;
	
	// temp
	W.border = 3;
	//W.cw = 10;
	//W.ch = 10;
	
	W.d = XOpenDisplay(NULL);
	W.scr = XDefaultScreen(W.d);
	W.vis = XDefaultVisual(W.d, W.scr);
	
	time_log("x stuff 1");
	
	FcInit();
	init_fonts("cascadia code:pixelsize=16:antialias=true:autohint=true", 0);
	
	W.cw = ceil(W.fonts[0].width);
	W.ch = ceil(W.fonts[0].height);
	
	W.w = W.cw*w+W.border*2;
	W.h = W.ch*h+W.border*2;
	
	W.cmap = XDefaultColormap(W.d, W.scr);
	
	Window parent = XRootWindow(W.d, W.scr);
	W.win = XCreateWindow(W.d, parent,
		0, 0, W.w, W.h, // geometry
		0, // border width
		XDefaultDepth(W.d, W.scr), // depth
		InputOutput, // class
		W.vis, // visual
		CWBackPixel | CWBorderPixel | CWBitGravity | CWEventMask | CWColormap, // value mask
		&(XSetWindowAttributes){
			//.background_pixel = TODO,
			//.border_pixel = TODO,
			.bit_gravity = NorthWestGravity,
			.event_mask = FocusChangeMask | KeyPressMask | KeyReleaseMask | ExposureMask | VisibilityChangeMask | StructureNotifyMask | ButtonMotionMask | ButtonPressMask | ButtonReleaseMask,
			.colormap = W.cmap,
		}
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
	XSync(W.d, False);
	
	init_hints();
	
	time_log("created window");
	
	update_charsize(W.cw, W.ch);
	
	init_term(&T, w, h);
	
	run();
	return 0;
}

XRenderColor get_color(Term* t, Color c, bool bold) {
	RGBColor rgb;
	if (c.truecolor)
		rgb = c.rgb;
	else {
		int i = c.i;
		if (i>=0 && i<256) {
			if (bold && i<8)
				i+=8;
			rgb = t->palette[i];
		}
		else if (i == -1)
			rgb = t->foreground;
		else
			rgb = t->background;
	}
	return (XRenderColor){
		.red = rgb.r*65535/255,
		.green = rgb.g*65535/255,
		.blue = rgb.b*65535/255,
		.alpha = 65535,
	};
}

int same_color(XRenderColor a, XRenderColor b) {
	return a.red==b.red && a.green==b.green && a.blue==b.blue && a.alpha==b.alpha;
}

// do we need this??
void alloc_color(XRenderColor* col, XftColor* out) {
	XftColorAllocValue(W.d, W.vis, W.cmap, col, out);
}

void fill_bg(int x, int y, int w, int h, XRenderColor col) {
	XftColor xcol;
	alloc_color(&col, &xcol);
	XftDrawRect(W.draw, &xcol, W.border+W.cw*x, W.border+W.ch*y, W.cw*w, W.ch*h);
}

// todo: we should cache this for all the text onscreen mayb
int xmakeglyphfontspecs(XftGlyphFontSpec* specs, int len, const Cell cells[len], int x, int y) {
	float winx = W.border+x*W.cw;
	float winy = W.border+y*W.ch;
	
	unsigned short prevmode = USHRT_MAX;
	Font* font = &W.fonts[0];
	int frcflags = 0;
	float runewidth = W.cw;
	FcResult fcres;
	FcPattern *fcpattern, *fontpattern;
	FcFontSet *fcsets[] = {NULL};
	FcCharSet *fccharset;
	int numspecs = 0;
	
	float xp = winx, yp = winy+font->ascent;
	for (int i=0; i<len; i++) {
		// Fetch rune and mode for current glyph.
		Char rune = cells[i].chr;
		Attrs attrs = cells[i].attrs;
		
		
		/* Skip dummy wide-character spacing. */
		if (cells[i].wide == -1)
			continue;
		
		/* Determine font for glyph if different from previous glyph. */
		//if (prevmode != mode) {
		//	prevmode = mode;
		frcflags = 0;
		runewidth = W.cw * (cells[i].wide==1 ? 2 : 1);
		if (attrs.italic && attrs.weight==1) {
			frcflags = 3;
		} else if (attrs.italic) {
			frcflags = 2;
		} else if (attrs.weight==1) {
			frcflags = 1;
		}
		font = &W.fonts[frcflags];
		yp = winy + font->ascent;
		//}

		/* Lookup character index with default font. */
		FT_UInt glyphidx = XftCharIndex(W.d, font->match, rune);
		if (glyphidx) {
			specs[numspecs].font = font->match;
			specs[numspecs].glyph = glyphidx;
			specs[numspecs].x = (short)xp;
			specs[numspecs].y = (short)yp;
			xp += runewidth;
			numspecs++;
			continue;
		}

		/* Fallback on font cache, search the font cache for match. */
		int f;
		for (f=0; f<frclen; f++) {
			glyphidx = XftCharIndex(W.d, frc[f].font, rune);
			/* Everything correct. */
			if (glyphidx && frc[f].flags == frcflags)
				goto found;
			/* We got a default font for a not found glyph. */
			if (!glyphidx && frc[f].flags == frcflags && frc[f].unicodep == rune) {
				goto found;
			}
		}
		/* Nothing was found. Use fontconfig to find matching font. */
		if (!font->set)
			font->set = FcFontSort(0, font->pattern, 1, 0, &fcres);
		fcsets[0] = font->set;
		
		/*
		 * Nothing was found in the cache. Now use
		 * some dozen of Fontconfig calls to get the
		 * font for one single character.
		 *
		 * Xft and fontconfig are design failures.
		 */
		fcpattern = FcPatternDuplicate(font->pattern);
		fccharset = FcCharSetCreate();
		
		FcCharSetAddChar(fccharset, rune);
		FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
		FcPatternAddBool(fcpattern, FC_SCALABLE, 1);
		
		FcConfigSubstitute(0, fcpattern, FcMatchPattern);
		FcDefaultSubstitute(fcpattern);
		
		fontpattern = FcFontSetMatch(0, fcsets, 1, fcpattern, &fcres);
		
		/* Allocate memory for the new cache entry. */
		if (frclen >= frccap) {
			frccap += 16;
			frc = realloc(frc, frccap * sizeof(Fontcache));
		}
		
		frc[frclen].font = XftFontOpenPattern(W.d, fontpattern);
		if (!frc[frclen].font)
			die("XftFontOpenPattern failed seeking fallback font: %s\n",
				strerror(errno));
		frc[frclen].flags = frcflags;
		frc[frclen].unicodep = rune;
		
		glyphidx = XftCharIndex(W.d, frc[frclen].font, rune);
		
		f = frclen;
		frclen++;
		
		FcPatternDestroy(fcpattern);
		FcCharSetDestroy(fccharset);
	found:;
		specs[numspecs].font = frc[f].font;
		specs[numspecs].glyph = glyphidx;
		specs[numspecs].x = (short)xp;
		specs[numspecs].y = (short)yp;
		xp += runewidth;
		numspecs++;
	}

	return numspecs;
}

void draw_background(Term* t, int x, int y, Cell* c) {
	if (c->wide == -1)
		return;
	XRenderColor bg = get_color(t, c->attrs.background, 0);
	XftColor xcol;
	alloc_color(&bg, &xcol);
	int width = c->wide ? 2 : 1;
	XftDrawRect(W.draw, &xcol, W.border+W.cw*x, W.border+W.ch*y, W.cw*width, W.ch);
	if (W.cursor_drawn && W.cursor_x==x && W.cursor_y==y)
		W.cursor_drawn = false;
}

void draw_char(Term* t, int x, int y, Cell* c) {
	if (c->wide == -1)
		return;
	XftColor xcol;
	if (c->chr) {
		XRenderColor fg = get_color(t, c->attrs.color, c->attrs.weight==1);
		alloc_color(&fg, &xcol);
		XftGlyphFontSpec specs;
		xmakeglyphfontspecs(&specs, 1, c, x, y);
		XftDrawGlyphFontSpec(W.draw, &xcol, &specs, 1);
	}
	if (W.cursor_drawn && W.cursor_x==x && W.cursor_y==y)
		W.cursor_drawn = false;
}


void erase_cursor(void) {
	if (!W.cursor_drawn)
		return;
	int x = W.cursor_x;
	int y = W.cursor_y;
	XCopyArea(W.d, W.cursor, W.pix, W.gc, 0, 0, W.cw, W.ch, W.border+W.cw*x, W.border+W.ch*y);
	W.cursor_drawn = false;
}

void draw_cursor(int x, int y) {
	if (W.cursor_drawn)
		erase_cursor();
	// todo: adding border each time is a pain. can we specify an origin somehow?
	XCopyArea(W.d, W.pix, W.cursor, W.gc, W.border+W.cw*x, W.border+W.ch*y, W.cw, W.ch, 0, 0);
	
	Cell temp = T.current->rows[y][x]; //a copy
	temp.attrs.color = temp.attrs.background;
	temp.attrs.background = (Color) {
		.truecolor = true,
		.rgb = {0,128,0},
	};
	int width = temp.wide==1 ? 2 : 1;
	
	// this time we do NOT want it to overflow ever
	XftDrawSetClipRectangles(W.draw, x*W.cw+W.border, y*W.ch+W.border, &(XRectangle){
			.width = W.cw*width,
			.height = W.ch,
		}, 1);
	
	draw_background(&T, x, y, &temp);
	draw_char(&T, x, y, &temp);
	
	XftDrawSetClip(W.draw, 0);
	
	W.cursor_x = x;
	W.cursor_y = y;
	W.cursor_drawn = true;
}

void draw_line(Term* t, int y) {
	for (int x=0; x<t->width; x++) {
		draw_background(&T, x, y, &t->current->rows[y][x]);
	}
	for (int x=0; x<t->width; x++) {
		draw_char(t, x, y, &t->current->rows[y][x]);
	}
}

void draw_screen(Term* t) {
	for (int y=0; y<t->height; y++)
		draw_line(t, y);
	if (t->show_cursor)
		draw_cursor(t->c.x, t->c.y);
	repaint();
}

/*void draw_strikethrough(int x, int y, int w, XRenderColor col) {
	XftColor xcol;
	alloc_color(&col, &xcol);
	XftDrawRect(W.draw, &xcol, W.border+W.cw*x, W.border+W.ch*y+W.fonts[0].ascent+1, W.cw*w, 1);
}

void draw_underline(int x, int y, int w, XRenderColor col) {
	XftColor xcol;
	alloc_color(&col, &xcol);
	XftDrawRect(W.draw, &xcol, W.border+W.cw*x, W.border+W.ch*y+W.fonts[0].ascent*2/3, W.cw*w, 1);
	}*/

/*void render_line(Term* t, int width, Cell line[width], int y) {
	// first, draw the background colors
	XRenderColor prev;
	int prevstart = -1;
	int i;
	for (i=0; i<width; i++) {
		XRenderColor col = get_color(t, line[i].attrs.background);
		if (prevstart==-1) {
			prevstart = i;
		} else {
			if (!same_color(col, prev)) {
				fill_bg(prevstart, y, i-prevstart, 1, prev);
				prevstart = i;
			}
		}
		prev = col;
	}
	if (i>prevstart) {
		fill_bg(prevstart, y, i-prevstart, 1, prev);
	}
	
	// now characters
	// we also group them based on attributes to minimize the number of draws.
	Attrs preva;
	for (i=0; i<width; i++) {
		XRenderColor col = get_color(t, line[i].attrs.color);
		Attrs attrs = line[i].attrs;
		if (prevstart==-1) {
			prevstart = i;
		} else {
			if (!same_color(col, prev) || attrs.italic != preva.italic || attrs.bold != preva.bold ) {
				
				prevstart = i;
			}
		}
		prev = col;
		preva = attrs;
	}
	
	// next, the strikethroughs
	prevstart = -1;
	for (i=0; i<width; i++) {
		XRenderColor col = {0};
		if (line[i].attrs.strikethrough)
			col = get_color(t, line[i].attrs.color);
		if (prevstart==-1) {
			prevstart = i;
		} else {
			if (!same_color(col, prev)) {
				if (prev.alpha)
					draw_strikethrough(prevstart, y, i-prevstart, prev);
				prevstart = i;
			}
		}
		prev = col;
	}
	// and then underlines
	prevstart = -1;
	for (i=0; i<width; i++) {
		XRenderColor col = {0};
		if (!line[i].attrs.underline)
			col = get_color(t, line[i].attrs.color);
		if (prevstart==-1) {
			prevstart = i;
p		} else {
			if (!same_color(col, prev)) {
				if (prev.alpha)
					draw_underline(prevstart, y, i-prevstart, prev);
				prevstart = i;
			}
p		}
		prev = col;
	}
	}*/
