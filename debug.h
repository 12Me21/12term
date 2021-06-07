#pragma once

void die(const char *errstr, ...) __attribute__((format(printf, 1, 2)));
void time_log(const char* str);
void print(const char* str, ...) __attribute__((format(printf, 1, 2)));
