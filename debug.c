#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdio.h>
#include <stdarg.h>

#include "common.h"
#include "debug.h"

void die(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

static struct timespec first_time;
static struct timespec prev_time;

static int timediff(struct timespec t1, struct timespec t2) {
	return (t1.tv_sec-t2.tv_sec)*1000 + (t1.tv_nsec-t2.tv_nsec)/1E6;
}

void print(const char* str, ...) {
	va_list ap;
	va_start(ap, str);
	vfprintf(stderr, str, ap);
	va_end(ap);
}

void time_log(const char* str) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (str) {
		int ms = timediff(now, prev_time);
		int total = timediff(now, first_time);
		print("@ %dms [+%dms] %s\n", total, ms, str);
	} else
		first_time = now;
	prev_time = now;
}
