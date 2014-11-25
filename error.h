// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_UTIL_H
#define EIGHTCC_UTIL_H

#include <stdbool.h>

extern bool enable_warning;
extern bool dumpstack;
extern bool dumpsource;
extern bool warning_is_error;

#define error(...) errorf(__FILE__, __LINE__, __VA_ARGS__)
#define warn(...)  warnf(__FILE__, __LINE__, __VA_ARGS__)

#ifndef __8cc__
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

extern void errorf(char *file, int line, char *fmt, ...) NORETURN;
extern void warnf(char *file, int line, char *fmt, ...);

#endif
