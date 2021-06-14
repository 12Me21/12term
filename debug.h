#pragma once

__attribute__((noreturn)) __attribute__((format(printf, 1, 2))) void die(const char *errstr, ...);
void time_log(const char* str);
void print(const char* str, ...) __attribute__((format(printf, 1, 2)));
