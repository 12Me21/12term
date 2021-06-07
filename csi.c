#include "ctlseqs2.h"

static int get_arg(int n, int def) {
	if (n>=T.parse.argc || T.parse.argv[n]==0)
		return def;
	return T.parse.argv[n];
}

// get first arg; defaults to 1 if not set. (very commonly used)
static int get_arg01() {
	return T.parse.argv[0] ? T.parse.argv[0] : 1;
}

static void process_csi_command(Char c) {	
	int arg = T.parse.argv[0]; //will be 0 if no args were passed. this is intentional.
	
	switch (T.parse.csi_private) {
	default:
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
			print("unknown CSI private terminator: %c\n", (char)c);
		}
		T.parse.state = NORMAL;
		break;
	case '>':
		//whatever;
		T.parse.state = NORMAL;
		break;
	case 0:
		switch (c) {
		case '@': // insert blank =ich=
			insert_blank(get_arg01());
			break;
		case 'A': // cursor up =cuu= =cuu1=
			cursor_up(get_arg01());
			break;
		case 'B': // cursor down =cud=
			cursor_down(get_arg01());
			break;
		case 'C': // cursor right =cuf= =cuf1=
			cursor_right(get_arg01());
			break;
		case 'D': // cursor left =cub=
			cursor_left(get_arg01());
			break;
		case 'd':
			cursor_to(T.c.x, get_arg01()-1);
			break;
		case 'G': // cursor column absolute =hpa=
			cursor_to(get_arg01()-1, T.c.y);
			break;
		case 'H': // move cursor =clear= =cup= =home=
		case 'f':
			cursor_to(get_arg(1, 1)-1,	get_arg(0, 1)-1);
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
				// ehhh
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
			insert_lines(get_arg01());
			break;
		case 'M': // delete lines =dl= =dl1=
			delete_lines(get_arg01());
			break;
		case 'm': // set graphics modes =blink= =bold= =dim= =invis= =memu= =op= =rev= =ritm= =rmso= =rmul= =setab= =setaf= =sgr= =sgr0= =sitm= =smso= =smul= =rmxx= =setb24= =setf24= =smxx=
			process_sgr();
			break;
		case 'n':
			if (arg == 6) {
				tty_printf("\x1B[%d;%dR", T.c.y+1, T.c.x+1);
			} else {
				print("unknown device status report: %d\n", arg);
			}
			break;
		case 'P': // delete characters =dch= =dch1=
			delete_chars(get_arg01());
			break;
		case 'r': // set scroll region =csr=
			T.current->scroll_top = get_arg01()-1;
			T.current->scroll_bottom = get_arg(1, T.height);
			// is this supposed to move the cursor?
			break;
		//case 'S': // scroll text up =indn=
			//	break; // too tired for this right now
		case 't': //window ops
			// TODO
			break;
		case 'X': // erase characters =ech=
			erase_characters(get_arg01());
			break;
		case 'Z': // back tab =cbt=
			back_tab(get_arg01());
			break;
		default:
			print("unknown CSI terminator: %c\n", (char)c);
		}
		T.parse.state = NORMAL;
	}
	return;
 invalid:
	//todo
	print("unknown command args for %c\n", (char)c);
	return;
}

void process_csi_char(Char c) {
	if (c>='0' && c<='9') { // arg
		T.parse.argv[T.parse.argc-1] *= 10;
		T.parse.argv[T.parse.argc-1] += c - '0';
	} else if (c==':' || c==';') { // arg separator
		// technically ; and : are used for different things, but it's not really ambiguous so I won't bother distinguishing for now.
		
		// really I think the purpose of the colons is to allow for argument grouping.
		// because all the other codes are a single number, so if they are not supported, it's nbd
		// but multi-number codes can cause frame shift issues, if they aren't supported, then the terminal will interpret the later values as individual args which is wrong.
		
		T.parse.argc++;
		T.parse.argv[T.parse.argc-1] = 0;
	} else {
		// finished
		process_csi_command(c);
		T.parse.state = NORMAL;
	}
}
