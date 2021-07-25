// Debug and printing functions

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdio.h>
#include <stdarg.h>

#include "common.h"
#include "debug.h"

bool debug_enabled = true;

__attribute__((noreturn)) void die(const char *errstr, ...) {
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
const char* char_name(Char c) {
	static char buf[20];
	if (c>' ' && c<'~') {
		sprintf(buf, "'%c'", c);
		return buf;
	}
	if (c>=0 && c<=' ')
		return (char*[]){"NUL","SOH","STX","ETX","EOT","ENQ","ACK","BEL","BS","TAB","LF","VT","FF","CR","SO","SI","DLE","DC1","DC2","DC3","DC4","NAK","SYN","ETB","CAN","EM","SUB","ESC","FS","GS","RS","US","SP"}[c];
	if (c==0x7F)
		return "DEL";
	sprintf(buf, "U+%X", c);
	return buf;
}

void print(const char* str, ...) {
	if (!debug_enabled)
		return;
	va_list ap;
	va_start(ap, str);
	vfprintf(stderr, str, ap);
	va_end(ap);
}

void time_log(const char* str) {
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
