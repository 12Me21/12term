#pragma once

#include <X11/Xlib.h>

void on_selectionnotify(XEvent* e);
void on_propertynotify(XEvent* e);
void on_selectionrequest(XEvent* e);

void own_clipboard(utf8* which, utf8* string);
void request_clipboard(Atom which);
