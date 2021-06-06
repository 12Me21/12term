#include <stdbool.h>
#include <stdio.h>

#include "buffer.h"
//#include "coroutine.h"
#include "ctlseqs.h"
#include "debug.h"
#include "tty.h" //todo: get rid of this.

#define DEFAULT break; default
#define CASE(x) break; case(x)

enum parse_state {
	NORMAL,
	ESC,
	CSI_START,
	CSI,
	ESC_TEST,
	UTF8,
	ALTCHARSET,
	STRING,
};

enum string_command {
	DCS = 1,
	APC,
	PM,
	OSC,
	TITLE,
};

int limit(int x, int min, int max) {
	if (x<min)
		return min;
	if (x>max)
		return max;
	return x;
}

void process_sgr_color(int* i, Color* out) {
	int type = T.parse.argv[*i+1];
	switch (type) {
	case 2:;
		int r = T.parse.argv[*i+2];
		int g = T.parse.argv[*i+3];
		int b = T.parse.argv[*i+4];
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
		int c = T.parse.argv[*i+2];
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
void process_sgr() {
	int c = T.parse.argc;
#define SEVEN(x) x: case x+1: case x+2: case x+3: case x+4: case x+5: case x+6: case x+7
	for (int i=0; i<c; i++) {
		int a = T.parse.argv[i];
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
			T.c.attrs.reverse = true;
			break;
		case 28:
			T.c.attrs.invisible = true;
			break;
		case 29:
			T.c.attrs.strikethrough = true;
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
	}
}

void set_private_modes(bool state) {
	for (int i=0; i<T.parse.argc; i++) {
		int a = T.parse.argv[i];
		switch (a) {
		case 0: // ignore
			break;
		case 1: // application cursor mode
			// do we actually care about this lol??
			break;
		case 5: // reverse video eye bleeding mode
			break;
		case 6: // cursor origin mode??
			break;
		case 7: // wrap?
			break;
		case 12: // enable/disable cursor blink
			T.blink_cursor = state;
			break;
		case 25: // show/hide cursor
			T.show_cursor = state;
			break;
		case 1000: // send mouse position on button press
			break;
		case 1003: // All Motion Mouse Tracking
			break;
		case 1004: // Send FocusIn/FocusOut events
			break;
		case 1006: // SGR mouse mode
			break;
		case 1047: // to alt/main buffer
			if (state) {
				T.current = &T.buffers[1];
				// do we clear when already in alt screen?
				clear_region(0, 0, T.width, T.height);
			} else
				T.current = &T.buffers[0];
			break;
		case 1048: // save/load cursor
			if (state)
				T.saved_cursor = T.c;
			else
				T.c = T.saved_cursor;
			break;
		case 1049: // 1048 and 1049
			if (state) {
				T.saved_cursor = T.c;
				T.current = &T.buffers[1];
				clear_region(0, 0, T.width, T.height);
			} else {
				T.c = T.saved_cursor;
				T.current = &T.buffers[0];
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
		case '@':
			insert_blank(get_arg01());
			break;
		case 'A':
			cursor_up(get_arg01());
			break;
		case 'B':
		case 'e':
			cursor_down(get_arg01());
			break;
		case 'C':
		case 'a':
			cursor_right(get_arg01());
			break;
		case 'd':
			cursor_to(T.c.x, get_arg01()-1);
			break;
		case 'H':
		case 'f':
			cursor_to(get_arg(1, 1)-1,	get_arg(0, 1)-1);
			break;
		case 'J':
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
		case 'K':
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
		case 'P':
			delete_chars(get_arg01());
			break;
		case 'M':
			delete_lines(get_arg01());
			break;
		case 'm':
			process_sgr();
			break;
		case 'r':
			T.current->scroll_top = get_arg01()-1;
			T.current->scroll_bottom = get_arg(1, T.height);
			// is this supposed to move the cursor?
			break;
		case 'n':
			if (arg == 6) {
				tty_printf("\x1B[%d;%dR", T.c.y+1, T.c.x+1);
			} else {
				print("unknown device status report: %d\n", arg);
			}
			break;
		case 't': //window ops
			// TODO
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

// returns true if char was eaten
bool process_control_char(unsigned char c) {
	switch (c) {
	case '\a':
		// bel
		break;
	case '\t':
		cursor_to((T.c.x+8)/8*8, T.c.y);
		break;
	case '\b':
		backspace();
		break;
	case '\r':
		T.c.x = 0;
		break;
	case '\n':
		index(1);
		break;
	default:
		return false;
	}
	return true;
}

void process_escape_char(Char c) {
	switch (c) {
		// multi-char sequences
	case '[':
		T.parse.argc = 1;
		T.parse.argv[0] = 0;
		T.parse.state = CSI_START;
		return;
	case '#':
		T.parse.state = ESC_TEST;
		return;
	case '%':
		T.parse.state = UTF8;
		return;
	case '(':
	case ')':
	case '*':
	case '+':
		T.parse.state = ALTCHARSET;
		T.parse.charset = c-'(';
		return;
		
		// things that take string parameters
	case 'P':
		T.parse.state = STRING;
		T.parse.string_command = DCS;
		return;
	case '_':
		T.parse.state = STRING;
		T.parse.string_command = APC;
		return;
	case '^':
		T.parse.state = STRING;
		T.parse.string_command = PM;
		return;
	case ']':
		T.parse.state = STRING;
		T.parse.string_command = OSC;
		return;
	case 'k':
		T.parse.state = STRING;
		T.parse.string_command = TITLE;
		return;

		// single char sequences
	case 'n':
		break;
	case 'o':
		break;
	case 'D':
		break;
	case '=':
		break;
	case '>':
		break;
	default:
		print("unknown ESC char: %d\n", c);
	}
	T.parse.state = NORMAL;
}

static void process_char(Char c) {
	struct parse* const p = &T.parse;
	
	if (p->state == STRING) {
		// end of string
		// todo: maybe check other characters here just in case
		if (c==0x07 || c==0x18 || c==0x1A || c==0x1B || (c>=0x80 && c<=0x9F)) {
			print("finished string\n");
			// (then we want to process the string sequence)
			// and /sometimes/ also process the character itself
			p->state = NORMAL;
		} else {
			// add char to string if possible
			if (p->string_length < sizeof(p->string)) {
				p->string[p->string_length++] = c;
			} else { //string too long!!
				
			}
		}
		return;
	}
	
	if (c<256 && c>=0 && process_control_char(c))
		return;
	////////////////////////
	switch (p->state) {
		 //normal
	case 0:
		switch (c) {
		case '\x1B':
			p->state = ESC;
			break;
		default:
			put_char(c);
		}
		break;
		// escape
	case ESC:
		process_escape_char(c);
		break;
	case CSI_START:
		p->state = CSI;
		if (c=='?')
			p->csi_private = '?';
		else if (c=='>')
			p->csi_private = '>';
		else {
			p->csi_private = 0;
			process_csi_char(c);
		}
		break;
	case CSI:
		process_csi_char(c);
		break;
	case ALTCHARSET:
		if (c=='0')
			;
		else if (c=='B')
			;
		else
			print("unknown charset: %c\n", c);
		p->state = NORMAL;
		break;
		// ???
	default:
		print("unknown parse state? (%d)\n", p->state);
		p->state = NORMAL;
	}
}

// 128, 192, 224, 240, 248

char utf8_type[32] = {
	// 0 - ascii byte
	[16] = 1,1,1,1,1,1,1,1, // 1 - continuation byte
	[24] = 2,2,2,2, // 2 - start of 2 byte sequence
	[28] = 3,3, // 3 - start of 3 byte sequence
	[30] = 4, // 4 - start of 4 byte sequence
	[31] = -1, // invalid
};
	
Char utf8_buffer = 0;
int utf8_state = 0;
void process_chars(int len, char cs[len]) {
	for (int i=0; i<len; i++) {
		Char c = (int)(unsigned char)cs[i]; //important! we need to convert to unsigned before casting to int
		int type = utf8_type[c>>3]; // figure out what type of utf-8 byte this is (number of leading 1 bits) using a lookup table
		c = c & (1<<7-type)-1; // extract the data bits
		// process the byte:
		switch (utf8_state<<3 | type) {
		// starts of sequences (state=0)
		case 2: case 3: case 4:
			utf8_buffer = c<<6*(type-1);
			utf8_state = 3*(type-1);
			break;
		// final/only byte
		case 0:
			process_char(c);
			utf8_state = 0;
			break;
		case 3<<3 | 1:
		case 3*2+1<<3 | 1:
		case 3*3+2<<3 | 1:
			utf8_buffer |= c;
			process_char(utf8_buffer);
			utf8_buffer = 0;
			utf8_state = 0;
			break;
		// second to last byte
		case 3*2<<3 | 1:
		case 3*3+1<<3 | 1:
			utf8_buffer |= c<<6;
			utf8_state++;
			break;
		// 3rd to last byte
		case 3*3<<3 | 1:
			utf8_buffer |= c<<6*2;
			utf8_state++;
			break;
		//invalid!
		default:
			print("Invalid utf8! [%d]\n", cs[i]);
			utf8_buffer = 0;
			utf8_state = 0;
			break;
		}
	}
	//write_char(c[i]);
}
