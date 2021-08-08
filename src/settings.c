// note: this is NOT a configuration file!
// it just contains functions for loading settings, and their default values
// see `xresources-example.ad` for more information

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include "buffer.h"
#include "settings.h"
#include "x.h"
extern bool parse_x_color(const char* c, RGBColor* out);

Settings settings = {0};

// we could use _Generic here but gosh...
#define F_COLOR(name, class, field, ...) {name, class, "RGBColor", sizeof(RGBColor), XtOffsetOf(Settings, field), "RGBColor", (XtPointer)&(RGBColor)__VA_ARGS__}
#define F_INT(name, class, field, val) {name, class, "Int", sizeof(int), XtOffsetOf(Settings, field), "Int", (XtPointer)&(int){val}}
#define F_STRING(name, class, field, val) {name, class, "String", sizeof(char*), XtOffsetOf(Settings, field), "String", (XtPointer)(val)}
#define F_FLOAT(name, class, field, val) {name, class, "Float", sizeof(float), XtOffsetOf(Settings, field), "Float", (XtPointer)&(float){val}}
#define F_PAL(n, sn, ...) {"color" sn, "Color" sn, "RGBColor", sizeof(RGBColor), XtOffsetOf(Settings, palette)+n*sizeof(RGBColor), "RGBColor", (XtPointer)&(RGBColor)__VA_ARGS__}

XtResource rs[] = {
	F_COLOR("cursorColor", "CursorColor", cursorColor, {  0,192,  0}),
	F_COLOR("foreground", "Foreground", foreground, {  255, 255,255}),
	F_COLOR("background", "Background", background, {  0, 0, 0}),
	F_INT("cursorShape", "CursorShape", cursorShape, 2),
	F_INT("height", "Height", height, 24), //todo: change these class names
	F_INT("width", "Width", width, 80),
	F_STRING("faceName", "FaceName", faceName, "monospace"),
	F_FLOAT("faceSize", "faceSize", faceSize, 12.0),
	F_INT("saveLines", "SaveLines", saveLines, 2000),
	F_STRING("hyperlinkCommand", "HyperlinkCommand", hyperlinkCommand, "xdg-open"),
	F_STRING("termName", "TermName", termName, "xterm-12term"),
	F_PAL(0, "0", {  0,  0,  0}),
	F_PAL(1, "1", {170,  0,  0}),
	F_PAL(2, "2", {  0,170,  0}),
	F_PAL(3, "3", {170, 85,  0}),
	F_PAL(4, "4", {  0,  0,170}),
	F_PAL(5, "5", {170,  0,170}),
	F_PAL(6, "6", {  0,170,170}),
	F_PAL(7, "7", {170,170,170}),
	F_PAL(8, "8", { 85, 85, 85}),
	F_PAL(9, "9", {255, 85, 85}),
	F_PAL(10, "10", { 85,255, 85}),
	F_PAL(11, "11", {255,255, 85}),
	F_PAL(12, "12", { 85, 85,255}),
	F_PAL(13, "13", {255, 85,255}),
	F_PAL(14, "14", { 85,255,255}),
	F_PAL(15, "15", {255,255,255}),
};

Boolean string_to_rgb(Display* display, XrmValue* args, Cardinal* num_args, XrmValue* from, XrmValue* to, XtPointer* converter_data) {
	// this is bad, because it relies on values in the W struct etc.
	return parse_x_color((void*)from->addr, (void*)to->addr);
}

void load_settings(int* argc, char** argv) {
	XtSetTypeConverter("String", "RGBColor", string_to_rgb, NULL, 0, XtCacheNone, NULL);
	XtGetApplicationResources(W.W, (void*)&settings, rs, XtNumber(rs), NULL, 0);
}
