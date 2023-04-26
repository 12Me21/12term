// Defines the behavior of keys

#include "keymap.h"
#include "event.h"

#include <X11/keysym.h>

// modes
#define F_MODS_ARG 2
#define F_ARG_MODS 3
#define F_FUNCTION 10

#define FUNCTION(name) .mode=10, .func=(name)

#define FORMAT(a,b,c) .output=(a),.mode=(b),.arg=(c)

#define ESC "\x1B"
// ESC [ 27 ; <modifiers> ; <number> ~
#define GENERAL(n) FORMAT(ESC"[27;%d;%d~", F_MODS_ARG, (n))
// ESC O <char>
#define SS3_c(c) FORMAT(ESC"O%c", F_ARG_MODS, (c))
// ESC [ 1 ; <modifiers> <char>
#define CSI_1_M_c(c) FORMAT(ESC"[1;%d%c", F_MODS_ARG, (c))
// ESC [ <modifiers> <char>
#define CSI_M_c(c) FORMAT(ESC"[%d%c", F_MODS_ARG, (c))
// ESC [ <number> ; <modifier> ~
#define CSI_n_M_T(x) FORMAT(ESC"[%d;%d~", F_ARG_MODS, (x))
// ESC [ <number> ~
#define CSI_n_T(x) FORMAT(ESC"[%d~", F_ARG_MODS, (x))
// ESC [ <char>
#define CSI_c(x) FORMAT(ESC"[%c", F_ARG_MODS, (x))

// numpad too
#define KP2(name, ...) {XK_##name, __VA_ARGS__},\
	{XK_KP_##name, __VA_ARGS__}

#define XK_KP_Return XK_KP_Enter

// modifier masks
#define C ControlMask
#define S ShiftMask
#define M Mod1Mask
#define ANY -1
// ctrl + anything else
#define C_ANY -2

// todo: maybe make the modifiers list a 3 digit ternary value



KeyMap KEY_MAP[] = {
	  ////////////
	 // NORMAL //
	////////////

	// (backspace is delete)
	{XK_BackSpace, M  , ESC"\x7F"},
	{XK_BackSpace, ANY, "\x7F"   },
	
	// Arrow Keys, Home/End, ‘Begin’ (numpad 5)
	// application cursor mode
	KP2(Up   , 0  , SS3_c('A'), .app_cursor=1),
	KP2(Down , 0  , SS3_c('B'), .app_cursor=1),
	KP2(Right, 0  , SS3_c('C'), .app_cursor=1),
	KP2(Left , 0  , SS3_c('D'), .app_cursor=1),
	KP2(Begin, 0  , SS3_c('E'), .app_cursor=1),
	KP2(End  , 0  , SS3_c('F'), .app_cursor=1),
	KP2(Home , 0  , SS3_c('H'), .app_cursor=1),
	// unmodified
	KP2(Up   , 0  , CSI_c('A')    ),
	KP2(Down , 0  , CSI_c('B')    ),
	KP2(Right, 0  , CSI_c('C')    ),
	KP2(Left , 0  , CSI_c('D')    ),
	KP2(Begin, 0  , CSI_c('E')    ),
	KP2(End  , 0  , CSI_c('F')    ),
	KP2(Home , 0  , CSI_c('H')    ),
	// with modifiers
	KP2(Up   , ANY, CSI_1_M_c('A')),
	KP2(Down , ANY, CSI_1_M_c('B')),
	KP2(Right, ANY, CSI_1_M_c('C')),
	KP2(Left , ANY, CSI_1_M_c('D')),
	KP2(Begin, ANY, CSI_1_M_c('E')),
	KP2(End  , ANY, CSI_1_M_c('F')),
	KP2(Home , ANY, CSI_1_M_c('H')),
	
	// Shift+Tab. for me this sends iso_left_tab but I'll put both just in case
	{XK_Tab         , S  , CSI_c('Z')},
	{XK_ISO_Left_Tab, ANY, CSI_c('Z')},
	
	{XK_Tab, C_ANY, GENERAL('\t')},
	
	// Enter
	KP2(Return, M  , ESC"\r"),
	// from windows terminal: Ctrl+Enter sends \n
	// seems like a good idea to make these distinguishable
	KP2(Return, C  , "\n"   ),
	KP2(Return, C|M, ESC"\n"),
	KP2(Return, ANY, "\r"   ),
	
	// PageUp/PageDown/Insert/Delete
	KP2(Insert, 0  , CSI_n_T(2)  ),
	KP2(Delete, 0  , CSI_n_T(3)  ),
	KP2(Prior , 0  , CSI_n_T(5)  ),
	KP2(Next  , 0  , CSI_n_T(6)  ),
	KP2(Insert, ANY, CSI_n_M_T(2)),
	KP2(Delete, ANY, CSI_n_M_T(3)),
	KP2(Prior , ANY, CSI_n_M_T(5)),
	KP2(Next  , ANY, CSI_n_M_T(6)),
	
	// Function Keys
	{XK_F1, 0  , SS3_c('P')},
	{XK_F2, 0  , SS3_c('Q')},
	{XK_F3, 0  , SS3_c('R')},
	{XK_F4, 0  , SS3_c('S')},
	{XK_F1, ANY, CSI_1_M_c('P')},
	{XK_F2, ANY, CSI_1_M_c('Q')},
	{XK_F3, ANY, CSI_1_M_c('R')},
	{XK_F4, ANY, CSI_1_M_c('S')},
	// yes it does go  15, 17,18,19,20,21, 23,24
	{XK_F5 , 0  , CSI_n_T(15)},
	{XK_F6 , 0  , CSI_n_T(17)},
	{XK_F7 , 0  , CSI_n_T(18)},
	{XK_F8 , 0  , CSI_n_T(19)},
	{XK_F9 , 0  , CSI_n_T(20)},
	{XK_F10, 0  , CSI_n_T(21)},
	{XK_F11, 0  , CSI_n_T(23)},
	{XK_F12, 0  , CSI_n_T(24)},
	{XK_F5 , ANY, CSI_n_M_T(15)},
	{XK_F6 , ANY, CSI_n_M_T(17)},
	{XK_F7 , ANY, CSI_n_M_T(18)},
	{XK_F8 , ANY, CSI_n_M_T(19)},
	{XK_F9 , ANY, CSI_n_M_T(20)},
	{XK_F10, ANY, CSI_n_M_T(21)},
	{XK_F11, ANY, CSI_n_M_T(23)},
	{XK_F12, ANY, CSI_n_M_T(24)},
	
	  ////////////
	 // EXTRAS //
	////////////
	
	// xterm and others allow detection of ctrl+(other mods)+<char> for a few extra chars, using a standard format for responses:
	// ESC [ 26 ; <modifier mask+1> ; <key code> ~
	{XK_period, C_ANY, GENERAL('.')},
	{XK_comma, C_ANY, GENERAL(',')},
	// some more that aren't usually supported:
	{XK_grave, C_ANY, GENERAL('`')},
	{XK_asciitilde, C_ANY, GENERAL('~')},
	{XK_equal, C_ANY, GENERAL('=')},
	
	// I extend this to make ctrl+i different from tab, and ctrl+m different from \r
	// you will likely need to configure your editor to accept these
	{XK_i, C_ANY, GENERAL('i')},
	{XK_m, C_ANY, GENERAL('m')},
	
	//////////////
	// COMMANDS //
	//////////////
	
	// Ctrl+Shift+V -> paste clipboard
	{XK_V, C|S, FUNCTION(clippaste)},
	// copy text at cursor (limited, sorry for now)
	//{XK_C, C|S, FUNCTION(simplecopy)},
	
	//{XK_R, C|S, FUNCTION(reload_settings)},
	
	// end
	{0},
};
