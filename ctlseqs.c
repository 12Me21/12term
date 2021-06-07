#include <stdbool.h>
#include <stdio.h>

#include "ctlseqs.h"
#include "debug.h"
#include "tty.h" //todo: get rid of this.

#define DEFAULT break; default
#define CASE(x) break; case(x)

#include "ctlseqs2.h"

ParseState P;

static int limit(int x, int min, int max) {
	if (x<min)
		return min;
	if (x>max)
		return max;
	return x;
}

void process_sgr_color(int* i, Color* out) {
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
void process_sgr(void) {
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
	for (int i=0; i<P.argc; i++) {
		int a = P.argv[i];
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
		case 1047: // to alt/main buffer
			if (state) {
				T.current = &T.buffers[1];
				// do we clear when already in alt screen?
				clear_region(0, 0, T.width, T.height);
			} else
				T.current = &T.buffers[0];
			dirty_all();
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
				T.current = &T.buffers[1];
				clear_region(0, 0, T.width, T.height);
			} else {
				T.current = &T.buffers[0];
				restore_cursor();
			}
			dirty_all();
			break;
		case 2004: // set bracketed paste mode
			T.bracketed_paste = state;
			break;
		default:
			print("unknown private mode: %d\n", a);
		}
	}
}

// returns true if char was eaten
bool process_control_char(unsigned char c) {
	switch (c) {
	case '\a':
		// bel
		break;
	case '\t':
		forward_tab(1);
		break;
	case '\b':
		backspace();
		break;
	case '\r':
		T.c.x = 0;
		break;
	case '\n':
	case '\v':
	case '\f':
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
		P.argc = 1;
		P.argv[0] = 0;
		P.state = CSI_START;
		return;
	case '(': // designate G0-G3 char sets
	case ')':
	case '*':
	case '+':
		P.state = ALTCHARSET;
		P.charset = c-'(';
		return;
		
		// things that take string parameters
	case 'P': // Device Control String
		P.state = STRING;
		P.string_command = DCS;
		return;
	case '_': // Application Program Command
		P.state = STRING;
		P.string_command = APC;
		return;
	case '^': // Privacy Message
		P.state = STRING;
		P.string_command = PM;
		return;
	case ']': // Operating System Command
		P.state = STRING;
		P.string_command = OSC;
		return;
		
	// single char sequences
	case '7': // Save Cursor
		save_cursor();
		break;
	case '8': // Restore Cursor
		restore_cursor();
		break;
	case 'E': // Next Line
		index(1);
		T.c.x = 0;
		break;
	case 'M': // Reverse Index
		reverse_index(1);
		break;
	case '=': // Application keypad
		break;
	case '>': // Normal keypad
		break;
	default:
		print("unknown ESC char: %d\n", c);
	}
	P.state = NORMAL;
}

static void process_char(Char c) {
	if (P.state == STRING) {
		// end of string
		// todo: maybe check other characters here just in case
		if (c==0x07 || c==0x18 || c==0x1A || c==0x1B || (c>=0x80 && c<=0x9F)) {
			// (then we want to process the string sequence)
			// TODO
			// and /sometimes/ also process the character itself
			P.state = NORMAL;
		} else {
			// add char to string if possible
			if (P.string_length < sizeof(P.string)) {
				P.string[P.string_length++] = c;
			} else { //string too long!!
				
			}
		}
		return;
	}
	
	if (c<256 && c>=0 && process_control_char(c))
		return;
	////////////////////////
	switch (P.state) {
		 //normal
	case NORMAL:
		switch (c) {
		case '\x1B':
			P.state = ESC;
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
		P.state = CSI;
		if (c=='?')
			P.csi_private = '?';
		else if (c=='>')
			P.csi_private = '>';
		else {
			P.csi_private = 0;
			process_csi_char(c);
		}
		break;
	case CSI:
		process_csi_char(c);
		break;
	case ALTCHARSET:
		if (c=='0' || c=='B')
			select_charset(P.charset, c);
		else
			print("unknown charset: %c\n", c);
		P.state = NORMAL;
		break;
		// ???
	default:
		print("unknown parse state? (%d)\n", P.state);
		P.state = NORMAL;
	}
}

// 128, 192, 224, 240, 248

static const char utf8_type[32] = {
	// 0 - ascii byte
	[16] = 1,1,1,1,1,1,1,1, // 1 - continuation byte
	[24] = 2,2,2,2, // 2 - start of 2 byte sequence
	[28] = 3,3, // 3 - start of 3 byte sequence
	[30] = 4, // 4 - start of 4 byte sequence
	[31] = -1, // invalid
};
	
static Char utf8_buffer = 0;
static int utf8_state = 0;
void process_chars(int len, const char cs[len]) {
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

void reset_parser(void) {
	utf8_state = 0;
	utf8_buffer = 0;
	P.state = NORMAL;
}
