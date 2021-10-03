#include "xftint.h"

_X_EXPORT FcPattern* XftFontMatch(Display* dpy, int screen, const FcPattern* pattern, FcResult* result) {
	
	FcPattern* new = FcPatternDuplicate (pattern);
	if (!new)
		return NULL;

	if (XftDebug () & XFT_DBG_OPENV) {
		printf ("XftFontMatch pattern ");
		FcPatternPrint (new);
	}
	FcConfigSubstitute (NULL, new, FcMatchPattern);
	if (XftDebug () & XFT_DBG_OPENV) {
		printf ("XftFontMatch after FcConfig substitutions ");
		FcPatternPrint (new);
	}
	XftDefaultSubstitute (dpy, screen, new);
	if (XftDebug () & XFT_DBG_OPENV) {
		printf ("XftFontMatch after X resource substitutions ");
		FcPatternPrint (new);
	}
	
	FcPattern* match = FcFontMatch (NULL, new, result);
	if (XftDebug () & XFT_DBG_OPENV) {
		printf ("XftFontMatch result ");
		FcPatternPrint (match);
	}
	FcPatternDestroy (new);
	return match;
}

_X_EXPORT XftFont* XftFontOpen (Display *dpy, int screen, ...) {
	va_list va;
	va_start(va, screen);
	FcPattern* pat = FcPatternVaBuild(NULL, va);
	va_end(va);
	if (!pat) {
		if (XftDebug () & XFT_DBG_OPEN)
			printf ("XftFontOpen: Invalid pattern argument\n");
		return NULL;
	}
	FcResult result;
	FcPattern* match = XftFontMatch (dpy, screen, pat, &result);
	if (XftDebug () & XFT_DBG_OPEN) {
		printf ("Pattern ");
		FcPatternPrint (pat);
		if (match) {
			printf ("Match ");
			FcPatternPrint (match);
		} else
			printf ("No Match\n");
	}
	FcPatternDestroy (pat);
	if (!match)
		return NULL;
	
	XftFont* font = XftFontOpenPattern (dpy, match);
	if (!font) {
		if (XftDebug () & XFT_DBG_OPEN)
			printf ("No Font\n");
		FcPatternDestroy (match);
	}
	
	return font;
}

_X_EXPORT XftFont* XftFontOpenName (Display *dpy, int screen, const char *name) {
	FcPattern	    *pat;
	FcPattern	    *match;
	FcResult	    result;
	XftFont	    *font;

	pat = FcNameParse ((FcChar8 *) name);
	if (XftDebug () & XFT_DBG_OPEN) {
		printf ("XftFontOpenName \"%s\": ", name);
		if (pat)
			FcPatternPrint (pat);
		else
			printf ("Invalid name\n");
	}

	if (!pat)
		return NULL;
	match = XftFontMatch (dpy, screen, pat, &result);
	if (XftDebug () & XFT_DBG_OPEN) {
		if (match) {
			printf ("Match ");
			FcPatternPrint (match);
		} else
			printf ("No Match\n");
	}
	FcPatternDestroy (pat);
	if (!match)
		return NULL;

	font = XftFontOpenPattern (dpy, match);
	if (!font) {
		if (XftDebug () & XFT_DBG_OPEN)
			printf ("No Font\n");
		FcPatternDestroy (match);
	}
	
	return font;
}
