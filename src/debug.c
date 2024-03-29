// Debug and printing functions

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "common.h"
#include "debug.h"

bool debug_enabled = true;
Debug_options DEBUG;

__attribute__((noreturn)) void die(const utf8 *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

static struct timespec first_time;
static struct timespec prev_time;

static long long timediff(struct timespec t1, struct timespec t2) {
	return (t1.tv_sec-t2.tv_sec)*1000L*1000*1000 + (t1.tv_nsec-t2.tv_nsec);
}

// returns a string containing a readable name for a character
const utf8* char_name(Char c) {
	static utf8 buf[20];
	if (c>' ' && c<'~') {
		sprintf(buf, "'%c'", c);
		return buf;
	}
	if (c>=0 && c<=' ')
		return (utf8*[]){"NUL","SOH","STX","ETX","EOT","ENQ","ACK","BEL","BS","TAB","LF","VT","FF","CR","SO","SI","DLE","DC1","DC2","DC3","DC4","NAK","SYN","ETB","CAN","EM","SUB","ESC","FS","GS","RS","US","SP"}[c];
	if (c==0x7F)
		return "DEL";
	sprintf(buf, "U+%X", c);
	return buf;
}

void print(const utf8* str, ...) {
	if (!debug_enabled)
		return;
	va_list ap;
	va_start(ap, str);
	vfprintf(stderr, str, ap);
	va_end(ap);
}

void time_log(const utf8* str) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (str) {
		long long ms = timediff(now, prev_time);
		long long total = timediff(now, first_time);
		print("@ %.2f ms [+%.2f ms] %s\n", total/1000/1000.0, ms/1000/1000.0, str);
	} else
		first_time = now;
	prev_time = now;
}

const char* debug_groups[] = {
	"open", "openv", "render", "draw", "ref", "glyph", "glyphv", "cache", "cachev", "memory",
	"redraw", "dirty", "utf8",
};

void debug_init(void) {
	utf8* e = getenv("DEBUG_12TERM");
	if (e) {
		print("DEBUG_12TERM is %s\n", e);
		// add leading and trailing spaces
		for (
			utf8 *word=e, *token;
			token = strtok(word, " ,\t\n\f");
			word=NULL
		) {
			for (int i=0; i<LEN(debug_groups); i++) {
				if (!strcmp(token, debug_groups[i])) {
					(&DEBUG.item_0)[i] = true;
					print("set: %s\n", debug_groups[i]);
					break;
				}
			}
		}
	}
}
