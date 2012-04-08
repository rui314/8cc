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

char *quote_cstring(char *p) {
    String *s = make_string();
    while (*p) {
        if (*p == '\"' || *p == '\\')
            string_appendf(s, "\\%c", *p);
        else if (*p == '\n')
            string_appendf(s, "\\n");
        else
            string_append(s, *p);
        p++;
    }
    return get_cstring(s);
}

char *quote_char(char c) {
    if (c == '\\') return format("'\\%c'", c);
    if (c == '\'') return format("'\\''");
    return format("'%c'", c);
}
