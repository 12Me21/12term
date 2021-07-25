#include "common.h"
#include "icon.h"

#include "incbin.h"

// note: I don't know if this will work consistently on systems with different endianness etc.
// as well as display formats other than 24 bit rgb of course
// but gosh the xpm decoder was so slow..
INCBIN(ICON_DATA, ICON_DATA_END, "icon.bin");

//(&ICON_DATA_END-&ICON_DATA_START)/4
int ICON_SIZE = 32;
