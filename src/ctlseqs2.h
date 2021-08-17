#pragma once
// This is for things which are shared between ctlseqs.c and csi.c

#include "common.h"

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
	ST,
};

enum string_command {
	DCS = 1,
	APC,
	PM,
	OSC,
};

typedef struct ParseState {
	enum parse_state state;
	
	utf8* string;
	int string_size;
	enum string_command string_command;
	int string_length;
	
	int argv[100];
	bool arg_colon[100];
	int argc;
	Char csi_private;
	Char csi_char;
	
	int charset;
	
	Char last_printed;
} ParseState;

extern ParseState P;

void process_csi_char(Char c);
void process_csi_command_2(Char c);
