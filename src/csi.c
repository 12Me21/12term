// Parsing control sequences starting with CSI (`ESC [`)

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

static bool process_sgr_color(int* i, Color* out) {
	int type = P.argv[*i+1];
	// TODO: check to make sure there are enough args left
	switch (type) {
	default:
		print("unknown SGR color type: %d\n", type);
		*i += 1; // do NOT change this to ++
		break;
	case 2:; // 2;<red>;<green>;<blue>
		int r = P.argv[*i+2];
		int g = P.argv[*i+3];
		int b = P.argv[*i+4];
		*i += 4;
		if (r<0||r>=256 || g<0||g>=256 || b<0||b>=256) {
			print("invalid rgb color in SGR: %d,%d,%d\n", r,g,b);
			break;
		}
		*out = (Color){
			.truecolor = true,
			.rgb = {r, g, b},
		};
		return true;
	case 5:; // 5;<palette index>
		int c = P.argv[*i+2];
		*i += 2;
		if (c<0 || c>=256) {
			print("invalid color index in SGR: %d\n", c);
			break;
		}
		*out = (Color){.i = c};
		return true;
	}
	return false;
}

// CSI [ ... m
static void process_sgr(void) {
	int c = P.argc;
#define SEVEN(x) x: case x+1: case x+2: case x+3: case x+4: case x+5: case x+6: case x+7
	for (int i=0; i<c; i++) {
		int a = P.argv[i];
		switch (a) {
		default:
			print("unknown sgr: %d\n", a);
			break;
		case 0: // reset
			T.c.attrs = (Attrs){
				.color = {.i=-1},
				.background = {.i=-2},
				// rest are set to 0
			};
			break;
		case 1: // bold
			T.c.attrs.weight = 1;
			break;
		case 2: // faint
			T.c.attrs.weight = -1; //this should blend fg with bg color maybe. but no one uses faint anyway so whatever
			break;
		case 3: // italic
			T.c.attrs.italic = true;
			break;
		case 4: // underline
			if (P.arg_colon[i]) { // 4:<type>
				i++;
				int t = P.argv[i];
				if (t>=0 && t<=5)
					T.c.attrs.underline = t;
			} else // normal
				T.c.attrs.underline = 1;
			break;
		case 5: //slow blink
		case 6: //fast blink
			// todo? Personally I have no interest in this since it's obnoxious and complicates rendering, but...
			break;
		case 7: // reverse colors
			T.c.attrs.reverse = true;
			break;
		case 8: // invisible (todo)
			T.c.attrs.invisible = true;
			break;
		case 9: // strikethrough
			T.c.attrs.strikethrough = true;
			break;
		// (10-20 are fonts)
		case 21: // double underline
			T.c.attrs.underline = 2;
			break;
		case 22: // bold/faint OFF
			T.c.attrs.weight = 0;
			break;
		case 23: // italic OFF
			T.c.attrs.italic = false;
			break;
		case 24: // underline OFF
			T.c.attrs.underline = 0;
			break;
		case 25: // blink OFF
			break;
		case 27: // reverse OFF
			T.c.attrs.reverse = false;
			break;
		case 28: // invisible OFF
			T.c.attrs.invisible = false;
			break;
		case 29: // strikethrough OFF
			T.c.attrs.strikethrough = false;
			break;
		case SEVEN(30): // set text color (0-7)
			T.c.attrs.color = (Color){.i = a-30};
			break;
		case 38: // set text color
			process_sgr_color(&i, &T.c.attrs.color);
			break;
		case 39: // reset text color
			T.c.attrs.color = (Color){.i = -1};
			break;
		case SEVEN(40): // set background color (0-7)
			T.c.attrs.background = (Color){.i = a-40};
			break;
		case 48: // set background color
			process_sgr_color(&i, &T.c.attrs.background);
			break;
		case 49: // reset background color
			T.c.attrs.background = (Color){.i = -2};
			break;
		// (50-55 are not widely used)
		case 58: // set underline color
			if (process_sgr_color(&i, &T.c.attrs.underline_color))
				T.c.attrs.colored_underline = true;
			break;
		case 59: // reset underline color (this means to match the text color I assume)
			T.c.attrs.colored_underline = false;
			T.c.attrs.underline_color = (Color){0}; // just zero this because why not
			break;
		// (60-75 not widely used)
		// (76-89 unused)
		case SEVEN(90): // set text color (8-15)
			T.c.attrs.color = (Color){.i = a-90+8};
			break;
		case SEVEN(100): // set background color (8-15)
			T.c.attrs.background = (Color){.i = a-100+8};
			break;
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

static void set_private_mode(int mode, bool state) {
	switch (mode) {
	default:
		print("unknown private mode: %d\n", mode);
		break;
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
	case 9: // X10 mouse compatibility mode (report on button press only)
	case 1000: // report on press+release, and send modifiers
	case 1002: // like 1000, but also reports motion if button is held
	case 1003: // like 1002, but always report motion
		T.mouse_mode = state ? mode : 0;
		break;
	case 1004: // report focus in/out events
		T.report_focus = state;
		break;
	case 1005: // utf-8 mouse encoding
	case 1006: // sgr mouse encoding
	case 1015: // urxvt mouse encoding
		T.mouse_encoding = state ? mode : 0;
		break;
	case 1047: // to alt/main buffer
		switch_buffer(state);
		break;
	case 1048: // save/load cursor
		if (state)
			save_cursor();
		else
			restore_cursor();
		break;
	case 1049: // 1048 and 1049
		if (state)
			save_cursor();
		switch_buffer(state);
		if (!state)
			restore_cursor();
		break;
	case 2004: // set bracketed paste mode
		T.bracketed_paste = state;
		break;
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
		default:
			dump(c);
			break;
		case 'h':
		case 'l':
			for (int i=0; i<P.argc; i++)
				set_private_mode(P.argv[i], c=='h');
			break;
		}
		break;
	case '>':
		//whatever;
		dump(c);
		break;
	case 0:
		switch (c) {
		default:
			print("UNKNOWN: ");
			dump(c);
			break;
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
			switch (arg) {
			default:
				goto invalid;
			case 0:
				T.tabs[T.c.x] = false;
				break;
			case 3:
				for (int i=0; i<T.width+1; i++)
					T.tabs[i] = false;
			}
			break;
		case 'H': // move cursor =clear= =cup= =home=
		case 'f': // (confirmed: eqv. in xterm)
			cursor_to(get_arg(1, 1)-1,	arg01()-1);
			break;
		case 'J': // erase lines =ed=
			switch (arg) {
			default:
				goto invalid;
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
			default:
				goto invalid;
			case 0: // clear line after cursor
				clear_region(T.c.x, T.c.y, T.width, T.c.y+1);
				break;
			case 1: // clear line before cursor
				clear_region(0, T.c.y, T.c.x, T.c.y+1);
				break;
			case 2: // entire line
				clear_region(0, T.c.y, T.width, T.c.y+1);
				break;
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
			switch (arg) {
			default:
				goto invalid;
			case 6:
				tty_printf("\x1B[%d;%dR", T.c.y+1, T.c.x+1);
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
		case 'b': // repeat previous char
			if (P.last_printed >= 0) {
				int count = arg01();
				for (int i=0; i<count; i++)
					put_char(P.last_printed);
			}
			break;
		case ' ': case '$': case '#': case '"':
			P.csi_char = c;
			P.state = CSI_2;
			return;
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
