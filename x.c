#include <sys/types.h>
#include <unistd.h>
#include <locale.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/Xatom.h>

typedef struct Xw {
	Display* d;
	int scr;
	Visual* vis;
	Colormap cmap;
	Window win;
	Pixmap pix;
	GC gc;
	int w,h;
	int cw,ch;
	int border;
	XftDraw* draw;
	struct atoms {
		Atom xembed;
		Atom wm_delete_window;
		Atom net_wm_name;
		Atom net_wm_icon_name;
		Atom net_wm_pid;
		Atom utf8_string;
	} atoms;
} Xw;

Xw W;

typedef void (*HandlerFunc)(XEvent*);

#define XEMBED_FOCUS_IN  4
#define XEMBED_FOCUS_OUT 5

void on_clientmessage(XEvent* e) {
	if (e->xclient.message_type == W.atoms.xembed && e->xclient.format == 32) {
		if (e->xclient.data.l[1] == XEMBED_FOCUS_IN) {
			//win.mode |= MODE_FOCUSED;
			//xseturgency(0);
		} else if (e->xclient.data.l[1] == XEMBED_FOCUS_OUT) {
			//win.mode &= ~MODE_FOCUSED;
		}
	} else if (e->xclient.data.l[0] == W.atoms.wm_delete_window) {
		//ttyhangup();
		exit(0);
	}
}

static HandlerFunc handler[LASTEvent] = {
	[ClientMessage] = on_clientmessage,
};

void run(void) {
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
	
	while (1) {
	/*fd_set rfd;
	FD_ZERO(&rfd);
	int ttyfd;
	FD_SET(ttyfd, &rfd);
	int xfd = XConnectionNumber(W.d);
	FD_SET(xfd, &rfd);*/
		//if (XPending(W.d))
		//	;
		while (XPending(W.d)) {
			XNextEvent(W.d, &ev);
			if (XFilterEvent(&ev, None))
				continue;
			if (handler[ev.type])
				(handler[ev.type])(&ev);
		}
		XFlush(W.d);
	}
}

void setHints(void) {
	XSetWMProperties(W.d, W.win, NULL, NULL, NULL, 0, &(XSizeHints){
			.flags = PSize | PResizeInc | PBaseSize | PMinSize,
			.width = W.w,
			.height = W.h,
			.width_inc = W.cw,
			.height_inc = W.ch,
			.base_width = W.border * 2,
			.base_height = W.border * 2,
		}, &(XWMHints){
			.flags = InputHint, .input = 1
		}, NULL); //todo: class hint?
}

void init_atoms(void) {
	W.atoms.xembed = XInternAtom(W.d, "_XEMBED", False);
	W.atoms.wm_delete_window = XInternAtom(W.d, "WM_DELETE_WINDOW", False);
	W.atoms.net_wm_name = XInternAtom(W.d, "_NET_WM_NAME", False);
	W.atoms.net_wm_icon_name = XInternAtom(W.d, "_NET_WM_ICON_NAME", False);
	W.atoms.net_wm_pid = XInternAtom(W.d, "_NET_WM_PID", False);
	W.atoms.utf8_string = XInternAtom(W.d, "UTF8_STRING", False);
	if (W.atoms.utf8_string == None)
		W.atoms.utf8_string = XA_STRING;
}

int main(int argc, char* argv[argc+1]) {
	// hecking locale
	setlocale(LC_CTYPE, "");
	XSetLocaleModifiers("");
	
	W.w = 100;
	W.h = 100;
	
	W.d = XOpenDisplay(NULL);
	W.scr = XDefaultScreen(W.d);
	W.vis = XDefaultVisual(W.d, W.scr);
	
	FcInit();
	// todo: load fonts
	
	W.cmap = XDefaultColormap(W.d, W.scr);
	// todo: load colors
	
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
	// init pixmap
	W.pix = XCreatePixmap(W.d, W.win, W.w, W.h, DefaultDepth(W.d, W.scr));
	//XSetForeground(W.d, W.gc, fg);
	//XFillRectangle(W.d, W.pix, W.gc, 0, 0, W.w, W.h);
	
	W.draw = XftDrawCreate(W.d, W.pix, W.vis, W.cmap);
	
	// TODO: xim
	
	init_atoms();
	
	XSetWMProtocols(W.d, W.win, &W.atoms.wm_delete_window, 1);
	pid_t thispid = getpid();
	XChangeProperty(W.d, W.win, W.atoms.net_wm_pid, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&thispid, 1);
	
	XMapWindow(W.d, W.win);
	XSync(W.d, False);
	
	W.border = 3;
	
	setHints();
	run();
	return 0;
}
