#include "keymap.h"

#define ESC "\x1B"
const char* const AUTO_SEQ = ESC"[27;%d;%d~";

#define F_MODS_ARG 2
#define F_ARG_MODS 3

const char* const SS3_c_FMT = ESC"O%c";
#define SS3_c(c) SS3_c_FMT, F_ARG_MODS, (c)

const char* const CSI_1_M_c_FMT = ESC"[1;%d%c";
#define CSI_1_M_c(c) CSI_1_M_c_FMT, F_MODS_ARG, (c)

const char* const CSI_M_c_FMT = ESC"[%d%c";
#define CSI_M_c(c) CSI_M_c_FMT, F_MODS_ARG, (c)

const char* const CSI_n_M_T_FMT = ESC"[%d;%d~";
#define CSI_n_M_T(x) CSI_n_M_T_FMT, F_ARG_MODS, (x)

const char* const CSI_n_T_FMT = ESC"[%d~";
#define CSI_n_T(x) CSI_n_T_FMT, F_ARG_MODS, (x)

const char* const CSI_c_FMT = ESC"[%c";
#define CSI_c(x) CSI_c_FMT, F_ARG_MODS, (x)

#define C ControlMask
#define S ShiftMask
#define M Mod1Mask

// todo: maybe make the modifiers list a 3 digit ternary value

static KeyMap KEY_MAP_ARRAY[] = {
	// backspace is delete. yes yes it's dumb but that's how it is, get over it.
	{XK_BackSpace, 0, "\x7F"},
	{XK_BackSpace, M, ESC"\x7F"},
	
	{XK_Delete, 0, CSI_n_T(3)},
	{XK_Delete, -1, CSI_n_M_T(3)},
	
	{'I', M, AUTO_SEQ, 2, 'i'},
	{'I', C, AUTO_SEQ, 2, 'i'},
	{'i', M, AUTO_SEQ, 2, 'i'},
	{'i', C, AUTO_SEQ, 2, 'i'},
	
	{'M', M, AUTO_SEQ, 2, 'm'},
	{'M', C, AUTO_SEQ, 2, 'm'},
	{'m', M, AUTO_SEQ, 2, 'm'},
	{'m', C, AUTO_SEQ, 2, 'm'},
	
	{XK_period, -2, AUTO_SEQ, 2, '.'},
	{XK_i, -2, AUTO_SEQ, 2, 'i'},
	{XK_m, -2, AUTO_SEQ, 2, 'm'},
	
	{XK_Up   , 0, CSI_c('A')},
	{XK_Down , 0, CSI_c('B')},
	{XK_Right, 0, CSI_c('C')},
	{XK_Left , 0, CSI_c('D')},
	{XK_Up   , -1, CSI_1_M_c('A')},
	{XK_Down , -1, CSI_1_M_c('B')},
	{XK_Right, -1, CSI_1_M_c('C')},
	{XK_Left , -1, CSI_1_M_c('D')},
	
	{XK_Return, M  , ESC"\r"},
	{XK_Return, C  , "\n"},
	{XK_Return, C|M, ESC"\n"},
	{XK_Return, -1 , "\r"},
	
	// todo: maybe add a special mode for "modifier if not empty"
	{XK_Home, 0, CSI_c('H')},
	{XK_Home, -1, CSI_1_M_c('H')},
	
	{XK_End, 0, CSI_c('F')},
	{XK_End, -1, CSI_1_M_c('F')},
	
	{XK_F1, 0, SS3_c('P')},
	{XK_F2, 0, SS3_c('Q')},
	{XK_F3, 0, SS3_c('R')},
	{XK_F4, 0, SS3_c('S')},
	// yes it does go  15, 17,18,19,20,21, 23,24
	{XK_F5, 0, CSI_n_T(15)},
	{XK_F6, 0, CSI_n_T(17)},
	{XK_F7, 0, CSI_n_T(18)},
	{XK_F8, 0, CSI_n_T(19)},
	{XK_F9, 0, CSI_n_T(20)},
	{XK_F10, 0, CSI_n_T(21)},
	{XK_F11, 0, CSI_n_T(23)},
	{XK_F12, 0, CSI_n_T(24)},
	
	{XK_F1, -1, CSI_1_M_c('P')},
	{XK_F2, -1, CSI_1_M_c('Q')},
	{XK_F3, -1, CSI_1_M_c('R')},
	{XK_F4, -1, CSI_1_M_c('S')},
	{XK_F5, -1, CSI_n_M_T(15)},
	{XK_F6, -1, CSI_n_M_T(17)},
	{XK_F7, -1, CSI_n_M_T(18)},
	{XK_F8, -1, CSI_n_M_T(19)},
	{XK_F9, -1, CSI_n_M_T(20)},
	{XK_F10, -1, CSI_n_M_T(21)},
	{XK_F11, -1, CSI_n_M_T(23)},
	{XK_F12, -1, CSI_n_M_T(24)},
	
	{0},
};

KeyMap* KEY_MAP = &KEY_MAP_ARRAY[0];
