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

void process_sgr_color(Term* t, int* i, Color* out) {
	int type = t->parse.argv[*i+1];
	switch (type) {
	case 2:;
		int r = t->parse.argv[*i+2];
		int g = t->parse.argv[*i+3];
		int b = t->parse.argv[*i+4];
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
		int c = t->parse.argv[*i+2];
		if (c<0 || c>=256) {
			print("invalid color index in SGR: %d\n", c);
		} else {
			*out = (Color){.i = c};
		}
		break;
	default:
		print("unknown SGR color type: %d\n", type);
		// and then here we should skip the rest of the colon parameters
	}
}

void process_sgr(Term* t) {
	int c = t->parse.argc;
#define SEVEN(x) x: case x+1: case x+2: case x+3: case x+4: case x+5: case x+6: case x+7
	for (int i=0; i<c; i++) {
		int a = t->parse.argv[i];
		switch (a) {
		case 0:
			t->c.attrs = (Attrs){
				.color = {.i=-1},
				.background = {.i=-2},
				// rest are set to 0
			};
			break;
		case 1:
			t->c.attrs.weight = 1;
			break;
		case 2:
			t->c.attrs.weight = -1; //this should blend fg with bg color maybe. but no one uses faint anyway so whatever
			break;
		case 3:
			t->c.attrs.italic = true;
			break;
		case 4:
			t->c.attrs.underline = true;
			break;
		case 5:
			//slow blink
			break;
		case 6:
			//fast blink
			break;
		case 7:
			t->c.attrs.reverse = true; //todo: how to implement this nicely
			break;
		case 8:
			t->c.attrs.invisible = true;
			break;
		case 9:
			t->c.attrs.strikethrough = true;
			break;
		case 22:
			t->c.attrs.weight = 0;
			break;
		case 23:
			t->c.attrs.italic = false;
			break;
		case 24:
			t->c.attrs.underline = false;
			break;
		case 25:
			// disable blink
			break;
		case SEVEN(30):
			t->c.attrs.color = (Color){.i = a-30};
			break;
		case 38:
			process_sgr_color(t, &i, &t->c.attrs.color);
			break;
		case 39:
			t->c.attrs.color = (Color){.i = -1};
			break;
		case SEVEN(40):
			t->c.attrs.background = (Color){.i = a-40};
			break;
		case 48:
			process_sgr_color(t, &i, &t->c.attrs.background);
			break;
		case 49:
			t->c.attrs.background = (Color){.i = -2};
			break;
		case SEVEN(90):
			t->c.attrs.color = (Color){.i = a-90+8};
			break;
		case SEVEN(100):
			t->c.attrs.background = (Color){.i = a-100+8};
			break;
		default:
			print("unknown sgr: %d\n", a);
		}
	}
}

void set_private_modes(Term* t, bool state) {
	for (int i=0; i<t->parse.argc; i++) {
		int a = t->parse.argv[i];
		switch (a) {
		default:
			print("unknown private mode: %d\n", a);
		}
	}
}

void process_csi_command(Term* t, Char c) {	
	int arg = t->parse.argv[0]; //will be 0 if no args were passed. this is intentional.
	
	if (t->parse.csi_private) { // CSI ? <args> <char>
		switch (c) {
		case 'h':
			set_private_modes(t, true);
			break;
		case 'l':
			set_private_modes(t, false);
			break;
		default:
			print("unknown CSI private terminator: %c\n", (char)c);
		}
		t->parse.state = NORMAL;
	} else {  // CSI <args> <char>
		switch (c) {
		case 'm':
			process_sgr(t);
			break;
		case 'n':
			if (arg == 6) {
				tty_printf(t, "\x1B[%d;%dR", t->c.y+1, t->c.x+1);
			} else {
				print("unknown device status report: %d\n", arg);
			}
			break;
		default:
			print("unknown CSI terminator: %c\n", (char)c);
		}
		t->parse.state = NORMAL;
	}
}

void process_csi_char(Term* t, Char c) {
	if (c>='0' && c<='9') { // arg
		t->parse.argv[t->parse.argc-1] *= 10;
		t->parse.argv[t->parse.argc-1] += c - '0';
	} else if (c==':' || c==';') { // arg separator
		// technically ; and : are used for different things, but it's not really ambiguous so I won't bother distinguishing for now.
		
		// really I think the purpose of the colons is to allow for argument grouping.
		// because all the other codes are a single number, so if they are not supported, it's nbd
		// but multi-number codes can cause frame shift issues, if they aren't supported, then the terminal will interpret the later values as individual args which is wrong.
		
		t->parse.argc++;
		t->parse.argv[t->parse.argc-1] = 0;
	} else {
		// finished
		process_csi_command(t, c);
		t->parse.state = NORMAL;
	}
}

// returns true if char was eaten
bool process_control_char(Term* t, unsigned char c) {
	switch (c) {
	case '\t':
		break;
	case '\b':
		break;
	case '\r':
		t->c.x = 0;
		break;
	case '\n':
		index(t, 1);
		break;
	default:
		return false;
	}
	return true;
}

void process_escape_char(Term* t, Char c) {
	switch (c) {
		// multi-char sequences
	case '[':
		t->parse.argc = 1;
		t->parse.argv[0] = 0;
		t->parse.state = CSI_START;
		return;
	case '#':
		t->parse.state = ESC_TEST;
		return;
	case '%':
		t->parse.state = UTF8;
		return;
	case '(':
	case ')':
	case '*':
	case '+':
		t->parse.state = ALTCHARSET;
		t->parse.charset = c-'(';
		return;
		
		// things that take string parameters
	case 'P':
		t->parse.state = STRING;
		t->parse.string_command = DCS;
		return;
	case '_':
		t->parse.state = STRING;
		t->parse.string_command = APC;
		return;
	case '^':
		t->parse.state = STRING;
		t->parse.string_command = PM;
		return;
	case ']':
		t->parse.state = STRING;
		t->parse.string_command = OSC;
		return;
	case 'k':
		t->parse.state = STRING;
		t->parse.string_command = TITLE;
		return;

		// single char sequences
	case 'n':
		break;
	case 'o':
		break;
	case 'D':
		break;
	default:
		print("unknown ESC char: %d\n", c);
	}
	t->parse.state = NORMAL;
}

static void process_char(Term* t, Char c) {
	struct parse* const p = &t->parse;
	
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
	
	if (c<256 && c>=0 && process_control_char(t, c))
		return;
	////////////////////////
	switch (p->state) {
		 //normal
	case 0:
		switch (c) {
		case 0x1B:
			p->state = ESC;
			break;
		default:
			put_char(t, c);
		}
		break;
		// escape
	case ESC:
		process_escape_char(t, c);
		break;
	case CSI_START:
		p->state = CSI;
		if (c=='?')
			p->csi_private = true;
		else {
			p->csi_private = false;
			process_csi_char(t, c);
		}
		break;
	case CSI:
		process_csi_char(t, c);
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
};
	
Char utf8_buffer = 0;
int utf8_state = 0;
void process_chars(Term* t, int len, char cs[len]) {
	for (int i=0; i<len; i++) {
		Char c = (unsigned char)cs[i]; //important! we need to convert to unsigned first
		int type = utf8_type[c>>3]; // figure out what type of utf-8 byte this is
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
			process_char(t, c);
			utf8_state = 0;
			break;
		case 3<<3 | 1:
		case 3*2+1<<3 | 1:
		case 3*3+2<<3 | 1:
			utf8_buffer |= c;
			process_char(t, utf8_buffer);
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
