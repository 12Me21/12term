output:= 12term

srcs:= x tty debug buffer ctlseqs keymap csi draw input font

CFLAGS+= -Wall -Wextra -g -pedantic -std=c11 -O1

CFLAGS+= -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -Wno-parentheses
CFLAGS+= -Werror=implicit-function-declaration -Werror=incompatible-pointer-types

libs:= m rt X11 util Xft

pkgs:= fontconfig freetype2 harfbuzz



include .Nice.mk
