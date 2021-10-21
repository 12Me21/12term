#include "xftint.h"

int XftDebug(void) {
	static int initialized;
	static int debug;
	
	if (!initialized) {
		initialized = 1;
		utf8* e = getenv("XFT_DEBUG");
		if (e) {
			printf("XFT_DEBUG=%s\n", e);
			debug = atoi (e);
			if (debug <= 0)
				debug = 1;
		}
	}
	return debug;
}
