#include <stdbool.h>

#include "buffer.h"
//#include "coroutine.h"
#include "ctlseqs.h"
#include "debug.h"

#define DEFAULT break; default
#define CASE(x) break; case(x)

enum parse_state {
	NORMAL,
	ESC,
	CSI_START,
	CSI,
	ESC_TEST,
	UTF8,
	ALTCHARSET0,
	ALTCHARSET1,
	ALTCHARSET2,
	ALTCHARSET3,
	DCS,
	APC,
	PM,
	OSC,
	TITLE,
};

void process_sgr(Term* t) {
	int c = t->parse.argc;
	for (int i=0; i<c; i++) {
		int a = t->parse.argv[i];
		switch (a) {
		case 31:
			t->c.attrs.color = (Color){
				.i = 1,
			};
			break;
		default:
			print("unknown sgr: %d\n", a);
		}
	}
}

void process_csi_char(Term* t, Char c) {
	if (c>='0' && c<='9') { // arg
		t->parse.argv[t->parse.argc-1] *= 10;
		t->parse.argv[t->parse.argc-1] += c - '0';
	} else if (c==':' || c==';') { // arg separator
		// technically ; and : are used for different things, but it's not really ambiguous so I won't bother distinguishing for now.
		t->parse.argc++;
		t->parse.argv[t->parse.argc-1] = 0;
	} else if (t->parse.csi_private) { // CSI ? <args> <char>
		print("unknown CSI private terminator: %c\n", (char)c);
		t->parse.state = NORMAL;
	} else {  // CSI <args> <char>
		switch (c) {
		case 'm':
			process_sgr(t);
			break;
		}
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
		t->c.y++;
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
		t->parse.state = ALTCHARSET0;
		return;
	case ')':
		t->parse.state = ALTCHARSET1;
		return;
	case '*':
		t->parse.state = ALTCHARSET2;
		return;
	case '+':
		t->parse.state = ALTCHARSET3;
		return;

		// things that take string parameters
	case 'P':
		t->parse.state = DCS;
		t->parse.in_string = true;
		return;
	case '_':
		t->parse.state = APC;
		t->parse.in_string = true;
		return;
	case '^':
		t->parse.state = PM;
		t->parse.in_string = true;
		return;
	case ']':
		t->parse.state = OSC;
		t->parse.in_string = true;
		return;
	case 'k':
		t->parse.state = TITLE;
		t->parse.in_string = true;
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
	
	if (t->parse.in_string) {
		// end of string
		// todo: maybe check other characters here just in case
		if (c==0x07 || c==0x18 || c==0x1A || c==0x1B || (c>=0x80 && c<=0x9F)) {
			print("finished string\n");
			p->in_string = false;
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
		t->parse.state = CSI;
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
		Char c = cs[i];
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
