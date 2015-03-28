// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include "8cc.h"

bool enable_warning = true;
bool warning_is_error = false;

static void print_error(char *file, int line, char *level, char *fmt, va_list args) {
    fprintf(stderr, isatty(fileno(stderr)) ? "\e[1;31m[%s]\e[0m " : "[%s] ", level);
    fprintf(stderr, "%s:%d: %s: ", file, line, input_position());
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

void errorf(char *file, int line, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    print_error(file, line, "ERROR", fmt, args);
    va_end(args);
    exit(1);
}

void warnf(char *file, int line, char *fmt, ...) {
    if (!enable_warning)
        return;
    va_list args;
    va_start(args, fmt);
    print_error(file, line, warning_is_error ? "ERROR" : "WARN", fmt, args);
    va_end(args);
    if (warning_is_error)
        exit(1);
}
