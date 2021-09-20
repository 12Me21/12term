#include "xftint.h"

_X_HIDDEN int _XftMatchSymbolic(XftSymbolic* s, int n, const char* name, int def) {
	while (n--) {
		if (!FcStrCmpIgnoreCase((FcChar8*)s->name, (FcChar8*)name))
			return s->value;
		s++;
	}
	return def;
}
