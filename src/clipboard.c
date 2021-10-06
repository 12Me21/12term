// X11 clipboard is so fucked that I had to put this in a separate file
#include <X11/Xlib.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "x.h"
#include "clipboard.h"
#include "tty.h"
#include "buffer.h"

// todo: multiple selection types
static utf8* clipboard_data = NULL;

void own_clipboard(utf8* which, utf8* data) {
	FREE(clipboard_data);
	clipboard_data = data;
	if (data) {
		print("setting clipboard: %s\n", data);
		XSetSelectionOwner(W.d, W.atoms.clipboard, W.win, CurrentTime);
	}
}

void request_clipboard(Atom which) {
	XConvertSelection(W.d, which, W.atoms.utf8_string, which, W.win, CurrentTime);
}

static void update_events(void) {
	XChangeWindowAttributes(W.d, W.win, CWEventMask, &(XSetWindowAttributes){.event_mask = W.event_mask});
}

static void tty_paste_text(int len, const char text[len]) {
	if (T.bracketed_paste)
		tty_write(6, "\x1B[200~");
	tty_write(len, text);
	if (T.bracketed_paste)
		tty_write(6, "\x1B[201~");
}

// recieve selection data from another application
void on_selectionnotify(XEvent* e) {
	Atom property = None;
	if (e->type==SelectionNotify)
		property = e->xselection.property;
	else if (e->type==PropertyNotify)
		property = e->xproperty.atom;
	
	if (property==None)
		return;
	
	unsigned long ofs = 0;
	unsigned long rem;
	do {
		unsigned long nitems;
		int format;
		utf8* data;
		Atom type;
		if (XGetWindowProperty(W.d, W.win, property, ofs, BUFSIZ/4, False, AnyPropertyType, &type, &format, &nitems, &rem, (void*)&data)) {
			print("Clipboard allocation failed\n");
			return;
		}
		
		if (e->type==PropertyNotify && nitems==0 && rem==0) {
			// If there is some PropertyNotify with no data, then
			// this is the signal of the selection owner that all
			// data has been transferred. We won't need to receive
			// PropertyNotify events anymore.
			W.event_mask &= ~PropertyChangeMask;
			update_events();
		}
		
		if (type==W.atoms.incr) {
			// Activate the PropertyNotify events so we receive
			// when the selection owner does send us the next
			// chunk of data.
			W.event_mask |= PropertyChangeMask;
			update_events();
			
			// Deleting the property is the transfer start signal.
			XDeleteProperty(W.d, W.win, property);
			continue;
		}
		
		// replace \n with \r
		utf8* repl = data;
		utf8* last = data + nitems*format/8;
		while ((repl = memchr(repl, '\n', last-repl))) {
			*repl++ = '\r';
		}
		
		// todo: ok so we maybe don't want to route this directly to tty_paste_text
		tty_paste_text(nitems*format/8, data);
		
		XFree(data);
		// number of 32-bit chunks returned
		ofs += nitems*format/32;
	} while (rem>0);
	// Deleting the property again tells the selection owner to send the
	// next data chunk in the property.
	XDeleteProperty(W.d, W.win, property);
}

void on_propertynotify(XEvent* e) {
	XPropertyEvent* xpev = &e->xproperty;
	if (e->xproperty.state==PropertyNewValue) {
		Atom type = xpev->atom;
		if (type==XA_PRIMARY || type==W.atoms.clipboard)
			on_selectionnotify(e);
	}
}

// when another application requests access to our clipboard
void on_selectionrequest(XEvent* e) {
	XSelectionRequestEvent* xsre = (void*)e;
	XSelectionEvent xse = {
		.type = SelectionNotify,
		.requestor = xsre->requestor,
		.selection = xsre->selection,
		.target = xsre->target,
		.time = xsre->time,
		.property = None, // reject
	};
	if (xsre->property==None)
		xsre->property = xsre->target;
	
	// "TARGETS" request: send a list of supported types
	if (xsre->target==W.atoms.targets) {
		// respond with the supported type
		const Atom types[] = {W.atoms.utf8_string, XA_STRING};
		XChangeProperty(xsre->display, xsre->requestor, xsre->property, XA_ATOM, 32, PropModeReplace, (void*)types, LEN(types));
		xse.property = xsre->property;
	// "STRING" or "UTF8_STRING" request: send the actual data
	} else if (xsre->target==W.atoms.utf8_string || xsre->target==XA_STRING) {
		utf8* seltext = clipboard_data;
		/*if (xsre->selection==XA_PRIMARY) {
			seltext = clipboard_data;
		} else if (xsre->selection==W.atoms.clipboard) {
			//seltext = xsel.clipboard;
		} else {
			print("Unhandled clipboard selection 0x%lx\n", xsre->selection);
			return;
			}*/
		if (seltext) {
			XChangeProperty(xsre->display, xsre->requestor, xsre->property, xsre->target, 8, PropModeReplace, (void*)seltext, strlen(seltext));
			xse.property = xsre->property;
		}
	}
	
	// all done, send a notification to the listener
	if (!XSendEvent(xsre->display, xsre->requestor, 1, 0, (void*)&xse))
		print("Error sending SelectionNotify event\n");
}
