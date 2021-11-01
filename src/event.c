// x event handler functions and related

#include <X11/Xlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "event.h"
#include "x.h"
#include "buffer.h"
#include "keymap.h"
#include "tty.h"
#include "draw.h"
#include "settings.h"
#include "clipboard.h"

void activate_hyperlink(const char* url) {
	if (!settings.hyperlinkCommand)
		return;
	pid_t pid = fork();
	if (pid<0) { // error
		print("error starting hyperlink process\n");
		return;
	}
	if (pid==0) { // child
		close(0);
		close(1);
		close(2);
		int err = execlp(settings.hyperlinkCommand, settings.hyperlinkCommand, url, NULL);
		_exit(err);
		return;
	}
	// parent
	// whatever man
}

// only valid for inputs 0-2047
static char* utf8_char(Char c) {
	static char buffer[5];
	if (c<128) {
		sprintf(buffer, "%c", c);
	} else if (c<2048) {
		sprintf(buffer, "%c%c", c>>6, c&(1<<6)-1);
	}
	return buffer;
}

// returns false if the click was out of range.
static bool cell_at(Px x, Px y, int* ox, int* oy) {
	int cx = (x-W.border)/W.cw;
	int cy = (y-W.border)/W.ch;
	*ox = limit(cx, 0, T.width-1);
	*oy = limit(cy, 0, T.height-1);
	return cx==*ox && cy==*oy;
}

// returns true if the event was eaten
int ox=-1, oy=-1, oldbutton = 3;
static bool mouse_event(XEvent* ev) {
	if (!T.mouse_mode)
		return false;
	int type = ev->xbutton.type;
	bool click = type==ButtonPress || type==ButtonRelease;
	int button = ev->xbutton.button;
	int x, y;
	// todo: do we clamp this or just ignore, or...
	cell_at(ev->xbutton.x, ev->xbutton.y, &x, &y);
	bool moved = x!=ox || y!=oy;
	ox = x; oy = y;
	// filter out the events we actually need to report
	if (!(
		(T.mouse_mode==9 && type==ButtonPress) ||
		(T.mouse_mode==1000 && click) ||
		(T.mouse_mode==1002 && (click || (button && moved))) ||
		(T.mouse_mode==1003 && (click || moved))))
		return false;
	
	int data = 0;
	// this is an 8 bit value which encodes which button was pressed
	// bits 0-1: button number (by default: 0=left, 1=middle, 2=right, 3=release(any))
	// bits 2-4: flags for the modifier keys: shift, alt, ctrl
	// bit 5: motion flag
	// bit 6: if set, button number is interpreted as: 0=button4, 1=button5, 2=button6, 3=button7 (buttons 4/5 are for up/down scrolling, and 6/7 are for left/right (on touchpads and some mice))
	// bit 7: same as bit 6, except this encodes buttons 8-11 (most mice don't have these buttons, though)
	
	// all button releases send the same code, except in the SGR encoding, where there is a separate flag.
	
	if (click) {
		if (type==ButtonRelease && T.mouse_encoding!=1006) // release
			data |= 3;
		else if (button>=1 && button<=3) // left, middle, right
			data |= button-1;
		else if (button>=4 && button<=7) // scroll up/down/left/right
			data |= button-4 | 1<<6;
		else if (button>=8 && button<=11) // extra buttons
			data |= button-8 | 1<<7;
	} else if (type==MotionNotify) {
		data |= 1<<5;
	}
	
	if (T.mouse_mode!=9) {
		int mods = ev->xbutton.state;
		if (mods & ShiftMask) data |= 1<<2;
		if (mods & Mod1Mask) data |= 1<<3;
		if (mods & ControlMask) data |= 1<<4;
	}
	
	switch (T.mouse_encoding) {
	default: // ESC [ M `btn` `x` `y` (chars)
		if (' '+x+1<256 && ' '+y+1<256)
			tty_printf("\x1B[M%c%c%c", ' '+data, ' '+x+1, ' '+y+1);
		break;
	case 1005: // ESC [ M `btn` `x` `y` (utf-8 chars)
		if (' '+x+1<2048 && ' '+y+1<2048)
			tty_printf("\x1B[M%s%s%s", utf8_char(' '+data), utf8_char(' '+x+1), utf8_char(' '+y+1));
		break;
	case 1006: // sgr: ESC [ < `btn` ; `x` ; `y` ; `M/m` (decimal)
		tty_printf("\x1B[<%d;%d;%d%c", data, x+1, y+1, type==ButtonRelease?'m':'M');
		break;
	case 1015: // urxvt: ESC [ `btn` ; `x` ; `y` ; `M` (decimal)
		tty_printf("\x1B[%d;%d;%dM", data, x+1, y+1);
		break;
	}
	return true;
}

static void on_motionnotify(XEvent* ev) {
	mouse_event(ev);
}
static void on_buttonpress(XEvent* ev) {
	if (mouse_event(ev))
		return;
	int button = ev->xbutton.button;
	switch (button) {
	case 1:; // left click
		int x, y;
		if (cell_at(ev->xbutton.x, ev->xbutton.y, &x, &y)) {
			Cell* c = &T.current->rows[y]->cells[x];
			if (c->attrs.link && c->attrs.link-1<T.links.length) {
				char* url = T.links.items[c->attrs.link-1];
				print("clicked hyperlink to: %s\n", url);
				activate_hyperlink(url);
			}
		}
		break;
	case 4: // scrollup
		if (move_scrollback(2))
			force_redraw();
		break;
	case 5: // scroll down
		if (move_scrollback(-2))
			force_redraw();
		break;
	}
}
static void on_buttonrelease(XEvent* ev) {
	mouse_event(ev);
}

static void on_visibilitynotify(XEvent* ev) {
	//XVisibilityEvent* e = &ev->xvisibility;
	//print("visibility\n");
	//MODBIT(win.mode, e->state != VisibilityFullyObscured, MODE_VISIBLE);
}

static void on_expose(XEvent* e) {
	(void)e;
	//dirty_all();
	draw(true);
}

// when window is resized
static void on_configurenotify(XEvent* e) {
	change_size(e->xconfigure.width, e->xconfigure.height, false, false);
}

static void on_clientmessage(XEvent* e) {
	if (e->xclient.data.l[0] == W.atoms.wm_delete_window) {
		print("window closing\n");
		sleep_forever(true);
	}
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

static bool ximopen(Display* d) {
	ime.xim = XOpenIM(d, NULL, NULL, NULL);
	if (!ime.xim) {
		// in case your XMODIFIERS env var is set incorrectly (i.e. you just uninstalled ibus but it's still set to "@im=ibus")
		// we try loading it again with that setting overridden
		// which will use xim
		XSetLocaleModifiers("@im=none");
		ime.xim = XOpenIM(d, NULL, NULL, NULL);
		if (!ime.xim) {
			print("no input method\n");
			return false;
		}
	}
	
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
	
	return true;
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
	[PropertyNotify] = on_propertynotify,
	[SelectionRequest] = on_selectionrequest,
	[KeyPress] = on_keypress,
	[FocusIn] = on_focusin,
	[FocusOut] = on_focusout,
	[MotionNotify] = on_motionnotify,
	[ButtonPress] = on_buttonpress,
	[ButtonRelease] = on_buttonrelease,
};

void clippaste(void) {
	request_clipboard(W.atoms.clipboard);
}
