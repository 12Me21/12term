#include "common.h"
#include "ctlseqs2.h"

// csi sequence:
// CSI [private] [arguments...] char [char2]

static void dump(Char last) {
	print("CSI ");
	if (P.csi_private)
		print("%s ", char_name(P.csi_private));
	for (int i=0; i<P.argc; i++) {
		print("%d ", P.argv[i]);
		if (i<P.argc-1) {
			if (P.arg_colon[i])
				print(": ");
			else
				print("; ");
		}
	}
	if (P.csi_char)
		print("%s ", char_name(P.csi_char));
	print("%s ", char_name(last));
	print("\n");
}

static int limit(int x, int min, int max) {
	if (x<min)
		return min;
	if (x>max)
		return max;
	return x;
}

static void process_sgr_color(int* i, Color* out) {
	int type = P.argv[*i+1];
	switch (type) {
	case 2:;
		int r = P.argv[*i+2];
		int g = P.argv[*i+3];
		int b = P.argv[*i+4];
		*out = (Color){
			.truecolor = true,
			.rgb = (RGBColor){
				limit(r, 0, 255),
				limit(g, 0, 255),
				limit(b, 0, 255),
			},
		};
		*i += 4;
		break;
	case 5:;
		int c = P.argv[*i+2];
		if (c<0 || c>=256) {
			print("invalid color index in SGR: %d\n", c);
		} else {
			*out = (Color){.i = c};
		}
		*i += 2;
		break;
	default:
		print("unknown SGR color type: %d\n", type);
		// and then here we should skip the rest of the colon parameters
	}
}

// CSI [ ... m
static void process_sgr(void) {
	int c = P.argc;
#define SEVEN(x) x: case x+1: case x+2: case x+3: case x+4: case x+5: case x+6: case x+7
	for (int i=0; i<c; i++) {
		int a = P.argv[i];
		switch (a) {
		case 0:
			T.c.attrs = (Attrs){
				.color = {.i=-1},
				.background = {.i=-2},
				// rest are set to 0
			};
			break;
		case 1:
			T.c.attrs.weight = 1;
			break;
		case 2:
			T.c.attrs.weight = -1; //this should blend fg with bg color maybe. but no one uses faint anyway so whatever
			break;
		case 3:
			T.c.attrs.italic = true;
			break;
		case 4:
			T.c.attrs.underline = true;
			break;
		case 5:
			//slow blink
			break;
		case 6:
			//fast blink
			break;
		case 7:
			T.c.attrs.reverse = true; //todo: how to implement this nicely
			break;
		case 8:
			T.c.attrs.invisible = true;
			break;
		case 9:
			T.c.attrs.strikethrough = true;
			break;
		case 22:
			T.c.attrs.weight = 0;
			break;
		case 23:
			T.c.attrs.italic = false;
			break;
		case 24:
			T.c.attrs.underline = false;
			break;
		case 25:
			// disable blink
			break;
		case 27:
			T.c.attrs.reverse = false;
			break;
		case 28:
			T.c.attrs.invisible = false;
			break;
		case 29:
			T.c.attrs.strikethrough = false;
			break;
		case SEVEN(30):
			T.c.attrs.color = (Color){.i = a-30};
			break;
		case 38:
			process_sgr_color(&i, &T.c.attrs.color);
			break;
		case 39:
			T.c.attrs.color = (Color){.i = -1};
			break;
		case SEVEN(40):
			T.c.attrs.background = (Color){.i = a-40};
			break;
		case 48:
			process_sgr_color(&i, &T.c.attrs.background);
			break;
		case 49:
			T.c.attrs.background = (Color){.i = -2};
			break;
		case SEVEN(90):
			T.c.attrs.color = (Color){.i = a-90+8};
			break;
		case SEVEN(100):
			T.c.attrs.background = (Color){.i = a-100+8};
			break;
		default:
			print("unknown sgr: %d\n", a);
		}
		if (P.arg_colon[i]) {
			print("extra colon args to SGR command %d\n", a);
			while (P.arg_colon[i])
				i++;
		}
	}
}

static void set_modes(bool state) {
	for (int i=0; i<P.argc; i++) {
		int a = P.argv[i];
		switch (a) {
		default:
			print("unknown mode: %d\n", a);
		}
	}
}

static void set_private_modes(bool state) {
	for (int i=0; i<P.argc; i++) {
		int a = P.argv[i];
		switch (a) {
		case 0: // ignore
			break;
		case 1: // application cursor mode
			T.app_cursor = state;
			break;
			//case 5: // reverse video eye bleeding mode
			//break;
			//case 6: // cursor origin mode??
			//break;
			//case 7: // wrap?
			//break;
		case 12: // enable/disable cursor blink
			T.cursor_blink = state;
			break;
		case 25: // show/hide cursor
			T.show_cursor = state;
			break;
		case 1047: // to alt/main buffer
			if (state) {
				switch_buffer(1);
				// do we clear when already in alt screen?
				clear_region(0, 0, T.width, T.height);
			} else
				switch_buffer(0);
			break;
		case 1048: // save/load cursor
			if (state)
				save_cursor();
			else
				restore_cursor();
			break;
		case 1049: // 1048 and 1049
			if (state) {
				save_cursor();
				switch_buffer(1);
				clear_region(0, 0, T.width, T.height);
			} else {
				switch_buffer(0);	
				restore_cursor();
			}
			break;
		case 2004: // set bracketed paste mode
			T.bracketed_paste = state;
			break;
		default:
			print("unknown private mode: %d\n", a);
		}
	}
}

static int get_arg(int n, int def) {
	if (n>=P.argc || P.argv[n]==0)
		return def;
	return P.argv[n];
}

// get first arg; defaults to 1 if not set. (very commonly used)
static int arg01(void) {
	return P.argv[0] ? P.argv[0] : 1;
}

void process_csi_command_2(Char c) {
	switch (P.csi_private) {
	default: 
		dump(c);
		break;
	case 0:
		switch (P.csi_char) {
		case ' ':
			switch (c) {
			case 'q':
				set_cursor_style(P.argv[0]);
				break;
			default:
				dump(c);
				break;
			}
			break;
		default:
			dump(c);
			break;
		}
		break;
	}
	P.state = NORMAL;
}

static void process_csi_command(Char c) {
	int arg = P.argv[0]; //will be 0 if no args were passed. this is intentional.
	
	switch (P.csi_private) {
	default:
		print("unknown CSI private character: %d\n", P.csi_private);
		// unknown
		break;
	case '?':
		switch (c) {
		case 'h':
			set_private_modes(true);
			break;
		case 'l':
			set_private_modes(false);
			break;
		default:
			dump(c);
			break;
		}
		break;
	case '>':
		//whatever;
		dump(c);
		break;
	case 0:
		switch (c) {
		case '@': // insert blank =ich=
			insert_blank(arg01());
			break;
		case 'A': // cursor up =cuu= =cuu1=
			cursor_up(arg01());
			break;
		case 'B': // cursor down =cud=
			cursor_down(arg01());
			break;
		case 'C': // cursor right =cuf= =cuf1=
			cursor_right(arg01());
			break;
		case 'D': // cursor left =cub=
			cursor_left(arg01());
			break;
		case 'd':
			cursor_to(T.c.x, arg01()-1);
			break;
		case 'G': // cursor column absolute =hpa=
		case '`': // (confirmed: xterm treats these the same)
			cursor_to(arg01()-1, T.c.y);
			break;
		case 'g': // tab clear
			if (arg==0)
				T.tabs[T.c.x] = false;
			else if (arg==3)
				for (int i=0; i<T.width+1; i++)
					T.tabs[i] = false;
			break;
		case 'H': // move cursor =clear= =cup= =home=
		case 'f': // (confirmed: eqv. in xterm)
			cursor_to(get_arg(1, 1)-1,	arg01()-1);
			break;
		case 'J': // erase lines =ed=
			switch (arg) {
			case 0: // after cursor
				clear_region(T.c.x, T.c.y, T.width, T.c.y+1);
				clear_region(0, T.c.y+1, T.width, T.height);
				break;
			case 1: // before cursor
				clear_region(0, T.c.y, T.c.x, T.c.y+1);
				clear_region(0, 0, T.width, T.c.y);
				break;
			case 2: // whole screen
				clear_region(0, 0, T.width, T.height);
				break;
			case 3: // scollback
				// ehhh todo
				T.scrollback.lines = 0;
				break;
			}
			break;
		case 'K': // erase characters in line =el= =el1=
			switch (arg) {
			case 0: // clear line after cursor
				clear_region(T.c.x, T.c.y, T.width, T.c.y+1);
				break;
			case 1: // clear line before cursor
				clear_region(0, T.c.y, T.c.x, T.c.y+1);
				break;
			case 2: // entire line
				clear_region(0, T.c.y, T.width, T.c.y+1);
				break;
			default:
				goto invalid;
			}
			break;
		case 'L': // insert lines =il= =il1=
			insert_lines(arg01());
			break;
		case 'h':
			set_modes(true);
			break;
		case 'l':
			set_modes(false);
			break;
		case 'M': // delete lines =dl= =dl1=
			delete_lines(arg01());
			break;
		case 'm': // set graphics modes =blink= =bold= =dim= =invis= =memu= =op= =rev= =ritm= =rmso= =rmul= =setab= =setaf= =sgr= =sgr0= =sitm= =smso= =smul= =rmxx= =setb24= =setf24= =smxx=
			process_sgr();
			break;
		case 'n':
			if (arg == 6) {
				tty_printf("\x1B[%d;%dR", T.c.y+1, T.c.x+1);
			} else {
				goto invalid;
			}
			break;
		case 'P': // delete characters =dch= =dch1=
			delete_chars(arg01());
			break;
		case 'r': // set scroll region =csr=
			set_scroll_region(
				arg01()-1,
				get_arg(1, T.height) // note that we don't subtract 1 here
			);
			break;
		case 'S': // scroll text up =indn=
			scroll_up(arg01());
			break;
		case 'T': // scroll text down
			scroll_down(arg01());
			break;
			//case 't': //window ops
			// TODO
			//break;
		case 'X': // erase characters =ech=
			erase_characters(arg01());
			break;
		case 'Z': // back tab =cbt=
			back_tab(arg01());
			break;
		case ' ': case '$': case '#': case '"':
			P.csi_char = c;
			P.state = CSI_2;
			return;
		default:
			print("UNKNOWN: ");
			dump(c);
		}
	}
	P.state = NORMAL;
	return;
 invalid:
	print("unknown command args: ");
	dump(c);
	P.state = NORMAL;
}

void process_csi_char(Char c) {
	if (c>='0' && c<='9') { // arg
		P.argv[P.argc-1] *= 10;
		P.argv[P.argc-1] += c - '0';
	} else if (c==':' || c==';') { // arg separator
		// the purpose of the colons is to allow for argument grouping.
		// because all the other codes are a single number, so if they are not supported, it's nbd
		// but multi-number codes can cause frame shift issues, if they aren't supported, then the terminal will interpret the later values as individual args which is wrong.
		// so the colons allow you to know how many values to skip in this case
		P.arg_colon[P.argc-1] = c==':';
		P.argc++;
		P.argv[P.argc-1] = 0;
	} else {
		// finished
		P.arg_colon[P.argc-1] = false;
		process_csi_command(c);
	}
}
