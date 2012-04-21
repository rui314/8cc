// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include "8cc.h"

bool suppress_warning = false;

void errorf(char *file, int line, char *fmt, ...) {
    fprintf(stderr, isatty(fileno(stderr)) ? "\e[1;31m[ERROR]\e[0m " : "[ERROR] ");
    fprintf(stderr, "%s:%d: %s: ", file, line, input_position());
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

void warn(char *fmt, ...) {
    if (suppress_warning)
        return;
    fprintf(stderr, isatty(fileno(stderr)) ? "\e[1;31m[WARNING]\e[0m " : "[WARNING] ");
    fprintf(stderr, "%s: ", input_position());
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}
