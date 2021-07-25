#pragma once
#define INCBIN(name, end, file) __asm__(".section .rodata \n .global " #name ", " #end " \n .balign 16 \n " #name ": \n .incbin \"" file "\" \n .balign 1 \n " #end ":")
