output:= 12term

srcs:= x tty debug

CFLAGS+= -Wall -Wextra -g -pedantic

CFLAGS+= -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -Wno-type-limits

libs:= m rt X11 util Xft

pkgs:= fontconfig freetype2



include .Nice.mk
