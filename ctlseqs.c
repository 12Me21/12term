// Parsing control sequences

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "ctlseqs.h"
#include "ctlseqs2.h"
// messy
extern RGBColor parse_x_color(const char* c);
extern void set_title(char* c);
extern void change_font(const char* name);

ParseState P;

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
		forward_index(1);
		break;
	default:
		return false;
	}
	return true;
}

static void begin_string(int type) {
	P.state = STRING;
	P.string_command = type;
	P.string_length = 0;
}

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
		T.c.x = 0;
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

static int parse_number(char** str) {
	int num = 0;
	char* s = *str;
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

static void process_osc(void) {
	char* s = P.string;
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
			char* se = strchr(s, ';');
			T.palette[id] = parse_x_color(s);
			dirty_all();
			s = se;
		}
		break;
	case 10: // set foreground, background, cursor colors
	case 11:
	case 12:
		while (s && *s==';') {
			s++;
			RGBColor col = parse_x_color(s);
			switch (p) {
			case 10:
				T.foreground = col;
				break;
			case 11:
				T.background = col;
				break;
			case 12:
				T.cursor_color = col;
				break;
			}
			p++;
			dirty_all();
		}
		break;
	case 50: // 
		if (*s==';') {
			s++;
			change_font(s);
		}
	}
	return;
 invalid:
	print("Invalid OSC command: %s\n", P.string);
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
		if (P.state == STRING) {
			unsigned char c = cs[i];
			// TODO: do we want to run the utf-8 decoder all the time or...
			// end of string
			// todo: maybe check other characters here just in case
			if (c==0x07 || c==0x18 || c==0x1A || c==0x1B || (c>=0x80 && c<=0x9F)) {
				P.string[P.string_length] = '\0';
				switch (P.string_command) {
				default:
					print("unknown string command\n");
					break;
				case OSC:
					process_osc();
					break;
				}
				// (then we want to process the string sequence)
				// TODO
				// and /sometimes/ also process the character itself
				P.state = NORMAL;
			} else {
				// add char to string if possible
				if (P.string_length < LEN(P.string)-1) {
					P.string[P.string_length++] = c;
				} else { //string too long!!
					
				}
			}
		} else {
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
			case 3*1+0<<3 | 1: //1,0 2,1 3,2
			case 3*2+1<<3 | 1:
			case 3*3+2<<3 | 1:
				utf8_buffer |= c;
				process_char(utf8_buffer);
				utf8_buffer = 0;
				utf8_state = 0;
				break;
				// second to last byte
			case 3*2+0<<3 | 1: //2,0 3,1
			case 3*3+1<<3 | 1:
				utf8_buffer |= c<<6;
				utf8_state++;
				break;
				// 3rd to last byte
			case 3*3+0<<3 | 1: //3,0
				utf8_buffer |= c<<6*2;
				utf8_state++;
				break;
				//invalid!
			default:
				print("Invalid utf8! [%d]\n", cs[i]);
				process_char(0xFFFD);
				utf8_buffer = 0;
				utf8_state = 0;
				break;
			}
		}
	}
	//write_char(c[i]);
}

void reset_parser(void) {
	utf8_state = 0;
	utf8_buffer = 0;
	P.state = NORMAL;
}
