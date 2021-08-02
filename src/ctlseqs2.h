#pragma once
// This is for things which are shared between ctlseqs.c and csi.c

#include "common.h"

// things used by files which include this
#include "tty.h"
#include "buffer.h"
#include "buffer2.h"

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
	
	char string[1030]; // bytes
	enum string_command string_command;
	int string_length;
	
	int argv[100];
	bool arg_colon[100];
	int argc;
	char csi_private;
	char csi_char;
	
	int charset;
	
	Char last_printed;
} ParseState;

extern ParseState P;

void process_csi_char(Char c);
void process_csi_command_2(Char c);
