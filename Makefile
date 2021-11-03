# output executable
output = 12term

all: $(output) terminfo

# all the .c files
srcdir = src
srcs = x tty debug buffer ctlseqs keymap csi draw font event settings icon clipboard #lua
srcs += xft/dbg xft/dpy xft/freetype xft/glyphs xft/render xft/cache
srcs := $(srcs:=.c) #append .c to names

#lua_version = 5.2

# libs to include with -l<name>
libs = rt util
# rt: realtime extensions
# util: pty stuff

# arguments for pkg-config
pkgs = x11 xrender freetype2 fontconfig #lua$(lua_version) #//harfbuzz
# fontconfig: (loading fonts)
# freetype2: (font rendering)
# X11: X window system (graphics, input, etc.)



CFLAGS+= -g # include debug symbols
#CFLAGS+= -O2
#CFLAGS+= -s -Os
CFLAGS+= -Wall -Wextra -pedantic -std=c11 # turn on a bunch of warnings
CFLAGS+= -Wno-sign-compare -Wno-unused-parameter -Wno-missing-field-initializers -Wno-parentheses -Wno-char-subscripts # disable these warnings
CFLAGS+= -Werror=implicit-function-declaration -Werror=incompatible-pointer-types # make these warnings into errors



# Install the terminfo file

# call `tic -D` to figure out the location of terminfo files
# todo: check if this fails?
terminfo != tic -D 2>/dev/null | head -n1
terminfo:= $(terminfo)/x/xterm-12term
# create a rule with a consistent name so you can type `make terminfo` instead of `make ~/.terminfo/x/xterm-12term` or whatever
.PHONY: terminfo
terminfo: $(terminfo)
# actual terminfo rule
$(terminfo): terminfo.src
	@$(call print,$@,,$^,)
	@tic -x $<
clean_extra+= $(terminfo)



install ?= /usr/local

# install desktop and icon files
icon = $(install)/share/icons/hicolor/48x48/apps/12term.png
$(icon): 48x48.png
	@$(call print,$@,,$^,)
	@cp $< $@

desktop = $(install)/share/applications/12term.desktop
$(desktop): 12term.desktop
	@$(call print,$@,,$^,)
	@cp $< $@

bin = $(install)/bin/12term
$(bin): 12term
	@$(call print,$@,,$^,)
	@cp $< $@

.PHONY: install uninstall

install: $(bin) $(terminfo) $(icon) $(desktop)

uninstall:
	$(RM) $(terminfo)
	$(RM) $(bin)
	$(RM) $(icon)
	$(RM) $(desktop)



include .Nice.mk



# the compiler's dependency checker can't see assembly .incbin directives, so I have to add this manually.
$(junkdir)/icon.c.o: icon.bin
