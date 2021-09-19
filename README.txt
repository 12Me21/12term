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

# notes on code style:
I prefer `type* name` over `type *name`.
I understand that the second form is technically correct, because of, ex: `int *a, *b`
But, as long as you avoid those types of declarations, it won't matter, and I think the first form looks better.
