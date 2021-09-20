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

| name       | apt        | pacman |
|------------|------------|--------|
| xft        | libxft-dev | libxft |
(you also need x11, freetype2, and fontconfig, but these are all dependencies of xft)

# Configuration

See `xresources-example.ad`

# References:

- st source code (https://st.suckless.org/)
- https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
- xterm source code + behavior (https://invisible-island.net/xterm/)
- https://sw.kovidgoyal.net/kitty/protocol-extensions.html
- foot terminal source code (https://codeberg.org/dnkl/foot/)
- https://en.wikipedia.org/wiki/ANSI_escape_code (this page sucks but a lot of people probably read it so it's good to support the stuff listed there)
n

# Xft history
Xft is the library i use for text rendering,
(basically it's just an interface between the Freetype font library and the internal x graphics systems)

for many years, xft didn't support fonts with colored glyphs (i.e. emojis), but recently a patch was released which fixes this.
I don't think the patch will be merged within my lifetime (not to mention actually added to package managers), so I switched to my own custom compiled-in version of xft

