// x event handler functions and related

#include <X11/Xlib.h>

#include "common.h"
#include "event.h"
#include "x.h"
#include "buffer.h"
#include "keymap.h"
#include "tty.h"
#include "draw.h"

#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

// copied from ST: todo: rewrite this
int ox=-1, oy=-1, oldbutton = 3;
static void mouse_event(XEvent* ev) {
	if (!T.mouse_mode)
		return;
	
	int x = (ev->xbutton.x - W.border) / W.cw;
	int y = (ev->xbutton.y - W.border) / W.ch;
	// todo: do we clamp this or just ignore, or...
	if (x<0)
		x=0;
	if (y<0)
		y=0;
	if (x>T.width-1)
		x=T.width-1;
	if (y>T.height-1)
		y=T.height-1;
	int button = ev->xbutton.button;
	int mods = ev->xbutton.state;
	
	/* from urxvt */
	if (ev->xbutton.type == MotionNotify) {
		if (x==ox && y==oy)
			return;
		if (T.mouse_mode!=1002 && T.mouse_mode!=1003)
			return;
		if (T.mouse_mode==1002 && oldbutton==3)
			return;
		button = oldbutton + 32;
		ox = x;
		oy = y;
	} else {
		if (!T.mouse_sgr && ev->xbutton.type == ButtonRelease) {
			button = 3;
		} else {
			button -= Button1;
			if (button >= 7)
				button += 128 - 7;
			else if (button >= 3)
				button += 64 - 3;
		}
		if (ev->xbutton.type == ButtonPress) {
			oldbutton = button;
			ox = x;
			oy = y;
		} else if (ev->xbutton.type == ButtonRelease) {
			oldbutton = 3;
			/* MODE_MOUSEX10: no button release reporting */
			if (T.mouse_mode==9)
				return;
			if (button == 64 || button == 65)
				return;
		}
	}
	
	if (T.mouse_mode!=9) {
		button |= !!(mods & ShiftMask)<<2 | !!(mods & Mod4Mask)<<3 | !!(mods & ControlMask)<<4;
	}
	
	if (T.mouse_sgr) {
		tty_printf("\x1B[<%d;%d;%d%c", button, x+1, y+1, ev->xbutton.type==ButtonRelease ? 'm' : 'M');
	} else if (x+1<256-32 && y+1<256-32) { //todo: the utf8 version of this?
		tty_printf("\x1B[M%c%c%c", 32+button, 32+x+1, 32+y+1);
	}
}

static void on_motionnotify(XEvent* ev) {
	mouse_event(ev);
}

static void on_buttonpress(XEvent* ev) {
	mouse_event(ev);
}

static void on_buttonrelease(XEvent* ev) {
	mouse_event(ev);
}

static void on_visibilitynotify(XEvent* ev) {
	//XVisibilityEvent* e = &ev->xvisibility;
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

static void on_configurenotify(XEvent* e) {
	Px width = e->xconfigure.width;
	Px height = e->xconfigure.height;
	if (width==W.w && height==W.h)
		return;
	
	update_size((width-W.border*2)/W.cw, (height-W.border*2)/W.ch);
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
			W.event_mask &= ~PropertyChangeMask;
			XChangeWindowAttributes(W.d, W.win, CWEventMask, &(XSetWindowAttributes){.event_mask = W.event_mask});
		}
		
		if (type == W.atoms.incr) {
			/*
			 * Activate the PropertyNotify events so we receive
			 * when the selection owner does send us the next
			 * chunk of data.
			 */
			W.event_mask |= PropertyChangeMask;
			XChangeWindowAttributes(W.d, W.win, CWEventMask, &(XSetWindowAttributes){.event_mask = W.event_mask});
			
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

struct Ime {
	XIM xim;
	XIC xic;
	XPoint spot;
	XVaNestedList spotlist;
} ime;

void xim_spot(int x, int y) {
	if (!ime.xic)
		return;
	
	ime.spot = (XPoint){
		.x = W.border + x * W.cw,
		.y = W.border + (y+1) * W.ch,
	};
	XSetICValues(ime.xic, XNPreeditAttributes, ime.spotlist, NULL);
}

// this will be useful someday, perhaps
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

static bool match_modifiers(KeyMap* want, int got) {
	if (want->app_keypad && T.app_keypad != (want->app_keypad==1))
		return false;
	if (want->app_cursor && T.app_cursor != (want->app_cursor==1))
		return false;
	
	if (want->modifiers==-1)
		return true;
	if (want->modifiers==-2)
		return (got & ControlMask);
	
	if (want->modifiers!=(got&(ControlMask|ShiftMask|Mod1Mask)))
		return false;
	
	return true;
}

void on_keypress(XEvent* ev) {
	XKeyEvent* e = &ev->xkey;
	
	KeySym ksym;
	char buf[1024] = {0};
	int len = 0;
	
	Status status;
	if (ime.xic)
		len = Xutf8LookupString(ime.xic, e, buf, sizeof(buf)-1, &ksym, &status);
	else {
		len = XLookupString(e, buf, sizeof(buf)-1, &ksym, NULL);
		status = XLookupBoth;
	}
	
	if (status==XLookupKeySym || status==XLookupBoth) {
		//print("got key: %s. mods: %d\n", XKeysymToString(ksym), e->state);
		// look up keysym in the key mapping
		for (KeyMap* map=KEY_MAP; map->k; map++) {
			if (map->k==ksym && match_modifiers(map, e->state)) {
				if (map->mode==0) {
					tty_write(strlen(map->output), map->output);
				} else if (map->mode==10) {
					map->func();
				} else {
					int mods = !!(e->state & ShiftMask) | !!(e->state & Mod1Mask)<<1 | !!(e->state & ControlMask)<<2;
					if (map->mode==1)
						tty_printf(map->output, mods+1);
					else if (map->mode==2)
						tty_printf(map->output, mods+1, map->arg);
					else if (map->mode==3)
						tty_printf(map->output, map->arg, mods+1);
				}
				return;
			}
		}
	}
	// otherwise, the input is normal text
	if ((status==XLookupChars || status==XLookupBoth) && len>0) {
		if (e->state & Mod1Mask) {
			//if (IS_SET(MODE_8BIT)) {
			//	if (*buf < 0177) {
			//		Rune c = *buf | 0x80;
			//		len = utf8encode(c, buf);
			//	}
			//} else {
			memmove(&buf[1], buf, len);
			len++;
			buf[0] = '\x1B';
		}
		tty_write(len, buf);
	}
}

void on_focusin(XEvent* e) {
	if (e->xfocus.mode == NotifyGrab)
		return;
	
	if (ime.xic)
		XSetICFocus(ime.xic);
}

void on_focusout(XEvent* e) {
	if (e->xfocus.mode == NotifyGrab)
		return;
	
	if (ime.xic)
		XUnsetICFocus(ime.xic);
}

static void ximinstantiate(Display* d, XPointer client, XPointer call);

static void ximdestroy(XIM xim, XPointer client, XPointer call) {
	ime.xim = NULL;
	XRegisterIMInstantiateCallback(W.d, NULL, NULL, NULL, ximinstantiate, NULL);
	XFree(ime.spotlist);
}

static int xicdestroy(XIC xim, XPointer client, XPointer call) {
	ime.xic = NULL;
	return 1;
}

static int ximopen(Display* d) {
	ime.xim = XOpenIM(d, NULL, NULL, NULL);
	if (ime.xim == NULL)
		return 0;
	
	if (XSetIMValues(ime.xim, XNDestroyCallback, &(XIMCallback){.callback = ximdestroy}, NULL))
		print("XSetIMValues: Could not set XNDestroyCallback.\n");
	
	ime.spotlist = XVaCreateNestedList(0, XNSpotLocation, &ime.spot, NULL);
	
	if (ime.xic == NULL) {
		ime.xic = XCreateIC(ime.xim, XNInputStyle,
			XIMPreeditNothing | XIMStatusNothing,
			XNClientWindow, W.win,
			XNDestroyCallback, &(XICCallback){.callback = xicdestroy},
			NULL);
	}
	if (ime.xic == NULL)
		print("XCreateIC: Could not create input context.\n");
	
	return 1;
}

static void ximinstantiate(Display* d, XPointer client, XPointer call) {
	if (ximopen(d))
		XUnregisterIMInstantiateCallback(d, NULL, NULL, NULL, ximinstantiate, NULL);
}

void init_input(void) {
	// init xim
	if (!ximopen(W.d)) {
		XRegisterIMInstantiateCallback(W.d, NULL, NULL, NULL, ximinstantiate, NULL);
	}
}

const HandlerFunc HANDLERS[LASTEvent] = {
	[ClientMessage] = on_clientmessage,
	[Expose] = on_expose,
	[VisibilityNotify] = on_visibilitynotify,
	[ConfigureNotify] = on_configurenotify,
	[SelectionNotify] = on_selectionnotify,
	[KeyPress] = on_keypress,
	[FocusIn] = on_focusin,
	[FocusOut] = on_focusout,
	[MotionNotify] = on_motionnotify,
	[ButtonPress] = on_buttonpress,
	[ButtonRelease] = on_buttonrelease,
};

void clippaste(void) {
	XConvertSelection(W.d, W.atoms.clipboard, W.atoms.utf8_string, W.atoms.clipboard, W.win, CurrentTime);
}
