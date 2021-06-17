#pragma once

#include "common.h"

// things used by files which include this
#include "buffer.h"
#include "buffer2.h"
#include "tty.h"

typedef struct ParseState {
	char string[1030];
	int string_command;
	int string_length;
	int state;
	int argv[100];
	bool arg_colon[100];
	int argc;
	char csi_private;
	char csi_char;
	int charset;
} ParseState;

extern ParseState P;

enum parse_state {
	NORMAL,
	ESC,
	CSI_START,
	CSI,
	CSI_2,
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
void process_csi_command_2(Char c);
