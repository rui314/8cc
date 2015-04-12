// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include "8cc.h"

bool enable_warning = true;
bool warning_is_error = false;

static void print_error(char *line, char *pos, char *label, char *fmt, va_list args) {
    fprintf(stderr, isatty(fileno(stderr)) ? "\e[1;31m[%s]\e[0m " : "[%s] ", label);
    fprintf(stderr, "%s: %s: ", line, pos);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

void errorf(char *line, char *pos, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    print_error(line, pos, "ERROR", fmt, args);
    va_end(args);
    exit(1);
}

void warnf(char *line, char *pos, char *fmt, ...) {
    if (!enable_warning)
        return;
    char *label = warning_is_error ? "ERROR" : "WARN";
    va_list args;
    va_start(args, fmt);
    print_error(line, pos, label, fmt, args);
    va_end(args);
    if (warning_is_error)
        exit(1);
}

char *token_pos(Token *tok) {
    File *f = tok->file;
    if (!f)
        return "(unknown)";
    char *name = f->name ? f->name : "(unknown)";
    return format("%s:%d:%d", name, tok->line, tok->column);
}
