output:= st

srcs:= x

CFLAGS+= -Wall -Wextra -g -pedantic

CFLAGS+= -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -Wno-type-limits

defines:= VERSION=\"0.8.4\" _XOPEN_SOURCE=600

libs:= m rt X11 util Xft

pkgs:= fontconfig freetype2

######################################################################

include .Nice.mk
