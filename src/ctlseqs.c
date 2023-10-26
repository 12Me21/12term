// Parsing control sequences
#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "ctlseqs.h"
#include "ctlseqs2.h"
#include "tty.h"
#include "buffer.h"
#include "buffer2.h"
#include "draw2.h"
#include "settings.h"
// messy
extern void own_clipboard(utf8* which, utf8* string);
extern void set_title(utf8* c);
extern void change_font(const utf8* name);

ParseState P;

// returns true if char was eaten
bool process_control_char(utf8 c) {
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
		carriage_return();
		break;
	case '\n':
	case '\v':
	case '\f':
		forward_index(1);
		break;
	default:
		return false;
	}
	return true;
}

static void begin_string(int type) {
	P.state = STRING;
	P.string_size = 1030;
	ALLOC(P.string, P.string_size);
	P.string_command = type;
	P.string_length = 0;
}

// xterm's VTPrsTbl.c is an interesting idea
// ex: in the normal state, it looks up each char in `ansi_table`.
// if it reads ESC, it would look up `ansi_table["\e"]` and switch to that state, which is CASE_ESC
// then, in that state, it uses `esc_table` to decide how to handle the next char, etc

static void process_escape_char(Char c) {
	switch (c) {
		// multi-char sequences
	case '(': // designate G0-G3 char sets
	case ')':
	case '*':
	case '+':
		P.state = ALTCHARSET;
		P.charset = c-'(';
		return;
	case '[': // CSI
		P.argc = 1;
		P.argv[0] = 0;
		P.csi_private = 0;
		P.csi_char = 0;
		P.state = CSI_START;
		return;
		
	// things that take string parameters
	case 'P': // Device Control String
		begin_string(DCS);
		return;
	case ']': // Operating System Command
		begin_string(OSC);
		return;
	case '^': // Privacy Message
		begin_string(PM);
		return;
	case '_': // Application Program Command
		begin_string(APC);
		return;
		
	// single char sequences
	case '7': // Save Cursor
		save_cursor();
		break;
	case '8': // Restore Cursor
		restore_cursor();
		break;
	case '=': // Application Keypad
		T.app_keypad = true;
		break;
	case '>': // Normal Keypad
		T.app_keypad = false;
		break;
	case 'E': // Next Line
		forward_index(1);
		carriage_return();
		break;
	case 'M': // Reverse Index
		reverse_index(1);
		break;
	case 'c': // full reset
		full_reset();
		break;
		
	default:
		print("unknown control sequence: ESC %s\n", char_name(c));
	}
	P.state = NORMAL;
}

static void process_kitty(int args[128], int length, utf8 data[length]) {
	print("got kitty data\n");
}

static void process_apc(void) {
	utf8* s = P.string;
	if (s[0]=='G') { // kitty graphics
		s++;
		int args[128] = {
			['a'] = 't',
			['f'] = 32,
			['t'] = 'd',
		};
		utf8 key;
		utf8* valueStart;
		
		while (1) {
			if (*s==';') {// end of key=value parameters
				s++;
				break;
			}
			if (*s>='a'&&*s<='z' || *s>='A'&&*s<='Z') { // key name
				key = *s;
				s++;
				if (*s=='=') { // =
					s++;
					valueStart = s;
					while (*s!=',' && *s!=';')
						s++;
					// now we have the value
					if (*valueStart=='-' || *valueStart>='0'&&*valueStart<='9') {
						args[key] = atoi(valueStart); //number
					} else {
						args[key] = *valueStart; // just 1 char
					}
					if (*s==',')
						s++;
					else if (*s!=';') {
						print("invalid char after value in kitty seq\n");
						return; // error, invalid character after value
					}
				} else {
					print("missing = after key name '%c' in kitty seq\n", key);
					return; // error, missing = after key name
				}
			} else {
				print("invalid char in kitty seq\n");
				return;
				// idk invalid character
			}
		}
		process_kitty(args, P.string_length-(s-P.string), s);
	} else {
		print("unknown APC command (yes i know the C already stands for command shhh)\n");
	}
}

static int parse_number(utf8** str) {
	int num = 0;
	utf8* s = *str;
	while (*s>='0' && *s<='9') {
		num *= 10;
		num += *s - '0';
		s++;
	}
	if (s==*str) // fail: no digits
		return -1;
	//if (*s==';' || *s=='\0') {
	*str = s;
	return num;
	//	}
	//return -1; // fail: found char other than ; or end of string
}

static utf8* base64_decode(int len, utf8 input[len]) {
	static const int8_t base64_map[256] = {
		['='] = 0,
		['A'] = 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
		['a'] = 26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,
		['0'] = 52,53,54,55,56,57,58,59,60,61,
		['+'] = 62, ['-'] = 62,
		['/'] = 62, ['_'] = 63,
	};
	utf8* out;
	ALLOC(out, (len*6+7)/8+1);
	utf8* p = out;
	int buffer = 0;
	int bits = 0;
	for (int i=0; i<len; i++) {
		buffer <<= 6;
		buffer |= base64_map[input[i]];
		bits += 6;
		if (bits >= 8) {
			*p++ = buffer>>bits-8 & 0xFF;
			bits -= 8;
		}
	}
	if (bits) {
		// todo
	}
	*p = '\0';
	return out;
}

static void process_osc(void) {
	utf8* s = P.string;
	int p = parse_number(&s);
	//else if (*s!='\0') {// unexpected char
	//	print("Invalid OSC command: %s\n", P.string);
	//	return;
	//}
	switch (p) {
	default:
		print("Unknown OSC command: %d\n", p);
		break;
	case 0: // set window title + icon title
		if (*s==';') {
			s++;
			set_title(s);
		} else
			set_title(NULL);
		break;
	case 4: // change palette color
		while (s && *s==';') {
			s++;
			int id = parse_number(&s);
			if (id<0 || id>=256)
				goto invalid;
			if (*s!=';')
				goto invalid;
			s++;
			utf8* se = strchr(s, ';');
			parse_x_color(s, &T.palette[id]);
			dirty_all();
			s = se;
		}
		break;
	case 8: // set hyperlink
		// skip past params for now
		if (*s!=';') {
			print("invalid OSC 8 hyperlink\n");
			break;
		}
		s++;
		s = strchr(s, ';');
		if (!s) {
			print("invalid OSC 8 hyperlink\n");
			break;
		}
		s++;
		if (*s == '\0') {
			// reset (empty string)
			T.c.attrs.link = 0;
		} else {
			// set url
			print("hyperlink: %s\n", s);
			int n = new_link(s);
			if (n>=0)
				T.c.attrs.link = n+1;
		}
		break;
	case 10: // set foreground, background, cursor colors
	case 11:
	case 12:
		// what ??  i dont think this was written correctly..
		/*while (s && *s==';') {
			s++;
			parse_x_color(s, (RGBColor*[]){
				&T.foreground, &T.background, &T.cursor_color
			}[p-10]);
			p++;
		}
		dirty_all();*/
		break;
	case 50: // change font
		if (*s==';') {
			s++;
			change_font(s);
		}
		break;
	case 52:; // set clipboard
		if (*s==';')
			s++;
		utf8* se = strchr(s, ';');
		if (se) {
			*se = '\0';
			se++;
			int len = strlen(se);
			own_clipboard(s, base64_decode(len, se));
		}
		break;
	case 104:; // reset palette color
		// untested
		if (*s!=';') {
			// 0 params = reset entire palette
			memcpy(T.palette, settings.palette, sizeof(T.palette));
		} else {
			while (s && *s==';') {
				s++;
				int id = parse_number(&s);
				if (id<0 || id>=256)
					goto invalid;
				T.palette[id] = settings.palette[id];
			}
		}
		dirty_all();
		break;
	case 110:; // reset fg color
		T.foreground = settings.foreground;
		break;
	case 111:; // reset bg color
		T.background = settings.background;
		break;
	case 112:; // reset cursor color
		T.cursor_color = settings.cursorColor;
		break;
	}
	return;
 invalid:
	print("Invalid OSC command: %s\n", P.string);
}

static void end_string(void) {
	P.state = NORMAL;
	if (!P.string)
		return;
	P.string[P.string_length] = '\0';
	switch (P.string_command) {
	default:
		print("unknown string command\n");
		break;
	case OSC:
		process_osc();
		break;
	case APC:
		process_apc();
		break;
	}
	FREE(P.string);
	P.string_size = 0;
}

static void push_string_byte(utf8 c) {
	if (!P.string)
		return;
	if (P.string_length >= P.string_size-1) {
		if (P.string_size >= 100000) {
			FREE(P.string);
			return;
		}
		P.string_size += 1024;
		REALLOC(P.string, P.string_size);
	}
	P.string[P.string_length++] = c;
}

static void process_char(Char c) {
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
			P.last_printed = c; //todo: when to reset this?
			put_char(c);
		}
		break;
		// escape
	case ESC:
		process_escape_char(c);
		break;
	case CSI_START:
		P.state = CSI;
		if (c=='?' || c=='>' || c=='!')
			P.csi_private = c;
		else {
			P.csi_private = 0;
			process_csi_char(c);
		}
		break;
	case CSI:
		process_csi_char(c);
		break;
	case CSI_2:
		process_csi_command_2(c);
		break;
	case ALTCHARSET:
		if (c=='0' || c=='B')
			select_charset(P.charset, c);
		else
			print("unknown charset: %s\n", char_name(c));
		P.state = NORMAL;
		break;
		// ???
	case ST:
		if (c=='\\') { // got ESC+backslash (end string)
			end_string();
		} else { // got ESC+<some other char> (add to string)
			// todo: is this really the proper way to handle this
			push_string_byte('\x1B');
			push_string_byte(c);
			P.state = STRING;
		}
		break;
	default:
		print("unknown parse state? (%d)\n", P.state);
		P.state = NORMAL;
	}
}

static Char utf8_buffer = 0;
static int utf8_remaining = 0;
void process_chars(int len, const utf8 cs[len]) {
	// 128, 192, 224, 240, 248
	static const int8_t utf8_type[32] = {
		// 0 - ascii byte
		[16] = 1,1,1,1,1,1,1,1, // 1 - continuation byte
		[24] = 2,2,2,2, // 2 - start of 2 byte sequence
		[28] = 3,3, // 3 - start of 3 byte sequence
		[30] = 4, // 4 - start of 4 byte sequence
		[31] = -1, // invalid
	};
	
	for (int i=0; i<len; i++) {
		Char c = (unsigned char)cs[i]; //important! we need to convert to unsigned before casting to int
		if (P.state == STRING) {
			// start of ESC \ (string terminator)
			if (c==0x1B)
				P.state = ST;
			// end of string
			// todo: maybe check other characters here just in case
			else if (c==0x07 || c==0x18 || c==0x1A || (c>=0x80 && c<=0x9F) || c=='\n') {
				end_string();
			} else {
				push_string_byte(c);
			}
		} else {
			// outside of a string: start decoding utf-8
			
			int type = utf8_type[c>>3]; // figure out what type of utf-8 byte this is (number of leading 1 bits) using a lookup table
			c = c & (1<<7-type)-1; // extract the data bits
			// process the byte:
			if (type==1) { // continuation byte
				if (utf8_remaining>0) {
					utf8_remaining--;
					utf8_buffer |= c<<(6*utf8_remaining);
					if (utf8_remaining==0)
						process_char(utf8_buffer);
				} else {
					if (DEBUG.utf8)
						print("Invalid utf8! unexpected continuation byte\n");
				}
			} else if (type==-1) { // invalid
				if (DEBUG.utf8)
					print("Invalid utf8! invalid byte\n");
			} else { // start byte
				if (utf8_remaining!=0) {
					if (DEBUG.utf8)
						print("Invalid utf8! interrupted sequence\n");
					process_char(0xFFFD);
				}
				if (type==0) {
					utf8_remaining = 0;
					process_char(c);
				} else {
					utf8_remaining = type-1;
					utf8_buffer = c<<(6*utf8_remaining);
				}
			}
		}
	}
	//write_char(c[i]);
}

void reset_parser(void) {
	utf8_remaining = 0;
	utf8_buffer = 0;
	P.state = NORMAL;
	P.last_printed = -1;
}
