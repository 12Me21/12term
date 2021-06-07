#pragma once

#include "buffer.h"
#include "buffer2.h"
#include "debug.h"
#include "tty.h"

typedef struct ParseState {
	char string[1030];
	int string_command;
	int string_length;
	int state;
	int argv[100];
	bool arg_colon[100]; //todo
	int argc;
	char csi_private;
	int charset;
} ParseState;

extern ParseState P;

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

void process_csi_char(Char c);
void set_private_modes(bool state);
void process_sgr(void);
