#include "common.h"
#include "icon.h"

#define INCBIN(align, name, end, file) __asm__(".section .rodata \n .global " #name ", " #end " \n .balign " align " \n " #name ": \n .incbin \"" file "\" \n .balign 1 \n " #end ":")

// note: I don't know if this will work consistently on systems with different endianness etc.
// as well as display formats other than 24 bit rgb of course
// but gosh the xpm decoder was so slow..
INCBIN("1", ICON_DATA, ICON_DATA_END, "icon.bin");

//(&ICON_DATA_END-&ICON_DATA_START)/4
int ICON_SIZE = 32;
