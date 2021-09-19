#include "xftint.h"

_X_HIDDEN FcFontSet* XftListFontsPatternObjects(Display* dpy, int screen, FcPattern* pattern, FcObjectSet* os) {
	return FcFontList(NULL, pattern, os);
}

_X_EXPORT FcFontSet* XftListFonts(Display* dpy, int screen, ...) {
	va_list	    va;
	va_start (va, screen);
	
	FcPattern* pattern;
	FcPatternVapBuild(pattern, NULL, va);
	
	const char* first = va_arg(va, const char*);
	FcObjectSet* os;
	FcObjectSetVapBuild(os, first, va);
	
	va_end(va);
	
	FcFontSet* fs = XftListFontsPatternObjects(dpy, screen, pattern, os);
	FcPatternDestroy(pattern);
	FcObjectSetDestroy(os);
	return fs;
}
