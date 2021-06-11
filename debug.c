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

static long long timediff(struct timespec t1, struct timespec t2) {
	return (t1.tv_sec-t2.tv_sec)*1000L*1000*1000 + (t1.tv_nsec-t2.tv_nsec);
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
		long long ms = timediff(now, prev_time);
		long long total = timediff(now, first_time);
		print("@ %lld µs [+%lld µs] %s\n", total/1000, ms/1000, str);
	} else
		first_time = now;
	prev_time = now;
}
