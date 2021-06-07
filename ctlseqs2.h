#pragma once

#include "buffer.h"
#include "buffer2.h"
#include "debug.h"
#include "tty.h"

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
