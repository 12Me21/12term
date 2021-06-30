output:= 12term

srcs:= x tty debug buffer ctlseqs keymap csi draw font icon.xpm event

libs:= m rt X11 util Xft Xpm
# m: math
# rt: realtime extensions (do i need this?)
# X11: X
# util: pty stuff
# Xft: X FreeType interface
# Xpm: reading xpm image data (for icon)

pkgs:= fontconfig freetype2 #harfbuzz

CFLAGS+= -g
CFLAGS+= -Wall -Wextra -pedantic -std=c11
CFLAGS+= -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -Wno-parentheses -Wno-char-subscripts
CFLAGS+= -Werror=implicit-function-declaration -Werror=incompatible-pointer-types

all: $(output) terminfo

# change the variable defined by icon.xpm from `static char* icon_xpm[]` to `const char* ICON_XPM[]` so it can be linked as a separate c file.
icon.xpm.c: icon.xpm
	@$(call print,$@,,$^,)
	@sed 's`^static char .*`// THIS FILE IS GENERATED FROM icon.xpm\nconst char* ICON_XPM[] = {`g' $< >$@
# todo: would be nice if icon.xpm.c was in .junk/ too, to avoid the extra clean and .gitignore items
clean_extra+= icon.xpm.c

# call `tic -D` to figure out the location of terminfo files
terminfo!= tic -D 2>/dev/null | head -n1
terminfo:= $(terminfo)/x/xterm-12term
# create a rule with a consistent name so you can type `make terminfo` instead of `make ~/.terminfo/x/xterm-12term` or whatever
.PHONY: terminfo
terminfo: $(terminfo)
# actual terminfo rule
$(terminfo): 12term.term
	@$(call print,$@,,$^,)
	@tic -x $<
clean_extra+= $(terminfo)



include .Nice.mk


