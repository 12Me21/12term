all: $(output) terminfo
# output executable
output:= 12term

# all the .c files
srcs:= x tty debug buffer ctlseqs keymap csi draw font icon.xpm event

lua_version:= 5.2

# libs to include with -l<name>
libs:= m rt util
# m: math
# rt: realtime extensions (do i need this?)
# util: pty stuff

# arguments for pkg-config
pkgs:= fontconfig freetype2 x11 xpm xft #lua$(lua_version) #//harfbuzz
# fontconfig: (loading fonts)
# freetype2: (font rendering)
# X11: X window system (graphics, input, etc.)
# Xft: X FreeType interface (font rendering)
# Xpm: reading xpm image data (for icon)



CFLAGS+= -g # include debug symbols
CFLAGS+= -Wall -Wextra -pedantic -std=c11 # turn on a bunch of warnings
CFLAGS+= -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -Wno-parentheses -Wno-char-subscripts # disable these warnings
CFLAGS+= -Werror=implicit-function-declaration -Werror=incompatible-pointer-types # make these warnings into errors



# convert an xpm file generated by an image editor into a format that will work with my code.
# changes the variable defined by icon.xpm from `static char* icon_xpm[]` to `const char* ICON_XPM[]` so it can be linked as a separate c file.
icon.xpm.c: icon.xpm
	@$(call print,$@,,$^,)
	@sed 's`^static char .*`// THIS FILE IS GENERATED FROM icon.xpm\nconst char* ICON_XPM[] = {`g' $< >$@
# todo: would be nice if icon.xpm.c was in .junk/ too, to avoid the extra clean and .gitignore items
clean_extra+= icon.xpm.c



# Install the terminfo file

# call `tic -D` to figure out the location of terminfo files
# todo: check if this fails?
terminfo != tic -D 2>/dev/null | head -n1
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
