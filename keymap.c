#include "keymap.h"
#include "x.h"

// modes
#define F_MODS_ARG 2
#define F_ARG_MODS 3
#define F_FUNCTION 10

// these are mostly to reduce memory usage + make this file shorter
// many keys use similar sequences with different numbers/terminator chars

#define FORMAT(a,b,c) .output=(a),.mode=(b),.arg=(c)

// ESC [ 27 ; <modifiers> ; <number> ~
#define ESC "\x1B"
const char* const AUTO_SEQ = ESC"[27;%d;%d~";
#define GENERAL(n) FORMAT(AUTO_SEQ, F_MODS_ARG, (n))
// ESC O <char>
const char* const SS3_c_FMT = ESC"O%c";
#define SS3_c(c) FORMAT(SS3_c_FMT, F_ARG_MODS, (c))
// ESC [ 1 ; <modifiers> <char>
const char* const CSI_1_M_c_FMT = ESC"[1;%d%c";
#define CSI_1_M_c(c) FORMAT(CSI_1_M_c_FMT, F_MODS_ARG, (c))
// ESC [ <modifiers> <char>
const char* const CSI_M_c_FMT = ESC"[%d%c";
#define CSI_M_c(c) FORMAT(CSI_M_c_FMT, F_MODS_ARG, (c))
// ESC [ <number> ; <modifier> ~
const char* const CSI_n_M_T_FMT = ESC"[%d;%d~";
#define CSI_n_M_T(x) FORMAT(CSI_n_M_T_FMT, F_ARG_MODS, (x))
// ESC [ <number> ~
const char* const CSI_n_T_FMT = ESC"[%d~";
#define CSI_n_T(x) FORMAT(CSI_n_T_FMT, F_ARG_MODS, (x))
// ESC [ <char>
const char* const CSI_c_FMT = ESC"[%c";
#define CSI_c(x) FORMAT(CSI_c_FMT, F_ARG_MODS, (x))

// modifier masks

#define C ControlMask
#define S ShiftMask
#define M Mod1Mask
#define APPKEY 1<<10
#define NO_APPKEY 1<<11
#define APPCUR 1<<12
#define NO_APPCUR 1<<13
#define ANY -1
// ctrl + anything else
#define C_ANY -2

// todo: maybe make the modifiers list a 3 digit ternary value

static KeyMap KEY_MAP_ARRAY[] = {
	// backspace is delete. yes yes it's dumb but that's how it is, get over it.
	{XK_BackSpace, M, ESC"\x7F"},
	//{XK_BackSpace, C, "\b"},
	//{XK_BackSpace, C|M, ESC"\b"},
	{XK_BackSpace, ANY, "\x7F"},
	
	{XK_Delete, 0, CSI_n_T(3)},
	{XK_Delete, ANY, CSI_n_M_T(3)},
	
	{XK_Up   , 0, SS3_c('A'), .app_cursor=1},
	{XK_Down , 0, SS3_c('B'), .app_cursor=1},
	{XK_Right, 0, SS3_c('C'), .app_cursor=1},
	{XK_Left , 0, SS3_c('D'), .app_cursor=1},
	{XK_Up   , 0, CSI_c('A')},
	{XK_Down , 0, CSI_c('B')},
	{XK_Right, 0, CSI_c('C')},
	{XK_Left , 0, CSI_c('D')},
	{XK_Up   , ANY, CSI_1_M_c('A')},
	{XK_Down , ANY, CSI_1_M_c('B')},
	{XK_Right, ANY, CSI_1_M_c('C')},
	{XK_Left , ANY, CSI_1_M_c('D')},
	
	// from windows terminal:
	// ctrl+enter sends \n
	// seems like a good idea to make these distinguishable
	{XK_Return, M  , ESC"\r"},
	{XK_Return, C  , "\n"},
	{XK_Return, C|M, ESC"\n"},
	{XK_Return, ANY, "\r"},
	
	{XK_Home, 0, SS3_c('H'), .app_cursor=1},
	{XK_Home, 0, CSI_c('H')},
	{XK_Home, ANY, CSI_1_M_c('H')},
	{XK_End, 0, SS3_c('F'), .app_cursor=1},
	{XK_End, 0, CSI_c('F')},
	{XK_End, ANY, CSI_1_M_c('F')},
	
	// pageup/pagedown
	{XK_Prior, 0, CSI_n_T(5)},
	{XK_Prior, ANY, CSI_n_M_T(5)},
	{XK_Next, 0, CSI_n_T(6)},
	{XK_Next, ANY, CSI_n_M_T(6)},
	
	// Function Keys
	{XK_F1, 0, SS3_c('P')},
	{XK_F2, 0, SS3_c('Q')},
	{XK_F3, 0, SS3_c('R')},
	{XK_F4, 0, SS3_c('S')},
	{XK_F1, ANY, CSI_1_M_c('P')},
	{XK_F2, ANY, CSI_1_M_c('Q')},
	{XK_F3, ANY, CSI_1_M_c('R')},
	{XK_F4, ANY, CSI_1_M_c('S')},
	// yes it does go  15, 17,18,19,20,21, 23,24
	{XK_F5, 0, CSI_n_T(15)},
	{XK_F6, 0, CSI_n_T(17)},
	{XK_F7, 0, CSI_n_T(18)},
	{XK_F8, 0, CSI_n_T(19)},
	{XK_F9, 0, CSI_n_T(20)},
	{XK_F10, 0, CSI_n_T(21)},
	{XK_F11, 0, CSI_n_T(23)},
	{XK_F12, 0, CSI_n_T(24)},
	{XK_F5, ANY, CSI_n_M_T(15)},
	{XK_F6, ANY, CSI_n_M_T(17)},
	{XK_F7, ANY, CSI_n_M_T(18)},
	{XK_F8, ANY, CSI_n_M_T(19)},
	{XK_F9, ANY, CSI_n_M_T(20)},
	{XK_F10, ANY, CSI_n_M_T(21)},
	{XK_F11, ANY, CSI_n_M_T(23)},
	{XK_F12, ANY, CSI_n_M_T(24)},
	
	// xterm and others allow detection of ctrl+(other mods)+<char> for a few extra chars, using a standard format for responses:
	// ESC [ 26 ; <modifier mask+1> ; <key code> ~
	{XK_period, C_ANY, GENERAL('.')},
	{XK_comma, C_ANY, GENERAL(',')},
	// some more that aren't usually supported:
	{XK_grave, C_ANY, GENERAL('`')},
	{XK_asciitilde, C_ANY, GENERAL('~')},
	
	// I extend this to make ctrl+i different from tab, and ctrl+m different from enter
	// you will likely need to configure your editor to accept these
	{XK_i, C_ANY, GENERAL('i')},
	{XK_m, C_ANY, GENERAL('m')},
	
	// commands:
	{'V', C|S, .mode=F_FUNCTION, .func=clippaste},
	
	// end
	{0},
};

KeyMap* KEY_MAP = &KEY_MAP_ARRAY[0];
