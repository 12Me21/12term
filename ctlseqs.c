#include <stdio.h>

#include "common.h"
#include "ctlseqs.h"
#include "ctlseqs2.h"

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
		P.state = STRING;
		P.string_command = DCS;
		return;
	case ']': // Operating System Command
		P.state = STRING;
		P.string_command = OSC;
		return;
	case '^': // Privacy Message
		P.state = STRING;
		P.string_command = PM;
		return;
	case '_': // Application Program Command
		P.state = STRING;
		P.string_command = APC;
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
		index(1);
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
	//write_char(c[i]);
}

void reset_parser(void) {
	utf8_state = 0;
	utf8_buffer = 0;
	P.state = NORMAL;
}
