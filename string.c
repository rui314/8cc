// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "8cc.h"

#define INIT_SIZE 8

String *make_string(void) {
    String *r = malloc(sizeof(String));
    r->body = malloc(INIT_SIZE);
    r->nalloc = INIT_SIZE;
    r->len = 0;
    r->body[0] = '\0';
    return r;
}

static void realloc_body(String *s) {
    int newsize = s->nalloc * 2;
    char *body = malloc(newsize);
    strcpy(body, s->body);
    s->body = body;
    s->nalloc = newsize;
}

char *get_cstring(String *s) {
    return s->body;
}

int string_len(String *s) {
    return s->len;
}

void string_append(String *s, char c) {
    if (s->nalloc == (s->len + 1))
        realloc_body(s);
    s->body[s->len++] = c;
    s->body[s->len] = '\0';
}

void string_appendf(String *s, char *fmt, ...) {
    va_list args;
    for (;;) {
        int avail = s->nalloc - s->len;
        va_start(args, fmt);
        int written = vsnprintf(s->body + s->len, avail, fmt, args);
        va_end(args);
        if (avail <= written) {
            realloc_body(s);
            continue;
        }
        s->len += written;
        return;
    }
}

char *vformat(char *fmt, va_list ap) {
    String *s = make_string();
    va_list aq;
    for (;;) {
        int avail = s->nalloc - s->len;
        va_copy(aq, ap);
        int written = vsnprintf(s->body + s->len, avail, fmt, aq);
        va_end(aq);
        if (avail <= written) {
            realloc_body(s);
            continue;
        }
        s->len += written;
        return get_cstring(s);
    }
}

char *format(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *r = vformat(fmt, ap);
    va_end(ap);
    return r;
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
