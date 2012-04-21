// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_UTIL_H
#define EIGHTCC_UTIL_H

#define error(...)                              \
    errorf(__FILE__, __LINE__, __VA_ARGS__)

#define assert(expr)                                    \
    do {                                                \
        if (!(expr)) error("Assertion failed: " #expr); \
    } while (0)

#ifndef __8cc__
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

extern void errorf(char *file, int line, char *fmt, ...) NORETURN;
extern void warn(char *fmt, ...);
extern char *quote_cstring(char *p);
extern char *quote_char(char c);

#endif /* EIGHTCC_UTIL_H */
