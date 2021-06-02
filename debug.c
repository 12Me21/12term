#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void die(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

static int firsttime = 1;
static struct timespec prevtime;

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
	if (!firsttime) {
		int ms = timediff(now, prevtime);
		print("[%dms] %s\n", ms, str);
	}
	prevtime = now;
	firsttime = 0;
}
