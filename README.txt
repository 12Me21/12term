WORK IN PROGRESS!!

Much of the X and pseudoterminal code was originally borrowed from st (https://st.suckless.org/)
This probably complicates licensing, but fortunately I don't care :)

the goal is to match the behavior of xterm (minus some complex features that aren't used much)

(I hope to perhaps add image support eventually, using the kitty graphics protocol.)

Starting ~June 6th, i've been writing this terminal using itself (with emacs of course)

# Compiling

run `make`.

install with `sudo make install` (at your own risk!)

# Dependencies

┏━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━┓
┃   Name     ┃     apt           ┃   pacman   ┃
┡━━━━━━━━━━━━╇━━━━━━━━━━━━━━━━━━━╇━━━━━━━━━━━━┩
│ X11        │ libx11-dev        │ libx11     │
│ Xrender    │ libxrender-dev    │ libxrender │
│ FreeType   │ libfreetype-dev   │ freetype2  │
│ Fontconfig │ libfontconfig-dev │ fontconfig │
└────────────┴───────────────────┴────────────┘

# Configuration

See `xresources-example.ad`

# References:

- st source code (https://st.suckless.org/)
- https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
- xterm source code + behavior (https://invisible-island.net/xterm/)
- https://sw.kovidgoyal.net/kitty/protocol-extensions.html
- foot terminal source code (https://codeberg.org/dnkl/foot/)
- https://en.wikipedia.org/wiki/ANSI_escape_code (this page sucks but a lot of people probably read it so it's good to support the stuff listed there)

# Xft history

for many years, xft didn't support fonts with colored glyphs (i.e. emojis), but recently a patch was released which fixes this.
I don't think the patch will be merged within my lifetime (not to mention actually added to package managers), so I switched to my own custom compiled-in version of xft
