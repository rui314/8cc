// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_BUFFER_H
#define EIGHTCC_BUFFER_H

#include <stdarg.h>

typedef struct {
    char *body;
    int nalloc;
    int len;
} Buffer;

extern Buffer *make_buffer(void);
extern char *format(char *fmt, ...);
extern char *vformat(char *fmt, va_list args);
extern int buf_len(Buffer *b);
extern void buf_write(Buffer *b, char c);
extern void buf_printf(Buffer *b, char *fmt, ...);
extern char *buf_body(Buffer *b);
extern char *quote_cstring(char *p);
extern char *quote_char(char c);

#endif
