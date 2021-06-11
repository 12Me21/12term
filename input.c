#include <X11/Xlib.h>

#include "common.h"
#include "keymap.h"
#include "x.h"
#include "tty.h"
#include "input.h"

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

static bool match_modifiers(int want, int got) {
	// todo: add bit for application keypad/cursor mode or whatever
	if (want==-1)
		return true;
	if (want==-2)
		return (got & ControlMask);
	return want==got;
}

void on_keypress(XEvent *ev) {
	XKeyEvent *e = &ev->xkey;
	
	//if (IS_SET(MODE_KBDLOCK))
	//	return;
	
	KeySym ksym;
	char buf[64] = {0};
	int len = 0;
	
	Status status;
	if (ime.xic)
		len = XmbLookupString(ime.xic, e, buf, sizeof buf, &ksym, &status);
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
