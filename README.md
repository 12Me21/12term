Much of the X and pseudoterminal code is borrowed from st (https://st.suckless.org/)
This probably complicates licensing, but fortunately I don't care :)

the goal is to match the behavior of xterm (minus some complex features that aren't used much)

I hope to perhaps add image support eventually, using the kitty graphics protocol.

Starting ~June 6th, i've been writing this terminal using itself (with emacs of course)

# Compiling

run `make`.

install with `sudo make install` (at your own risk!)

# Dependencies

| name       | apt        | pacman |
|------------|------------|--------|
| xft        | libxft-dev | libxft |
(you also need x11, freetype2, and fontconfig, but these are all dependencies of xft)

# References:

- st source code
- https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
- xterm source code + behavior
- https://sw.kovidgoyal.net/kitty/protocol-extensions.html
- https://en.wikipedia.org/wiki/ANSI_escape_code (this page sucks but a lot of people probably read it so it's good to support the stuff listed there)

# Notes

## background color erase
This is a controversial issue... For now, my solution is:
1: bce is always enabled on the alternate screen
 - this is to improve compatibility and efficiency for 'graphical' programs, which run in the alternate screen
2: bce is enabled on the main screen, when using commands to scroll or insert/delete lines etc.
3: bce is DISABLED on the main screen, when printing newlines and when text wraps automatically.
 - this is so that, if a line break occurs in the middle of a highlighted region of text, it will not affect the background color of the new row.
 - I think I can get away with this, because it's rather uncommon to have large areas with custom background color on the main screen, so bce is rarely useful here.

## offscreen column
Terminals allow the cursor to be in the column just past the right edge of the screen. This, by itself, is good. BUT many terminals also have strange behaviors when in this state.
For example, in xterm: if the cursor is in this offscreen column, `ESC [ K` (clear rest of line) will *MOVE THE CURSOR 1 space to the left*
This causes very serious issues, because this sequence is commonly used to reset the background color as a workaround for BCE, and so in rare cases you'll lose 1 character at the edge of the screen. (I've heard this happens in both grep and gcc, for example)
I've chosen to not implement these quirks, because as far as I know there is no advantage to doing this.

# um

todo: ok so i renamed buffer1.h to buffer.h and got an error running make
one of the dependency files listed buffer1.h and it didn't realize nothing needed it
how to fix?