// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

#define INIT_SIZE 8

Buffer *make_buffer() {
    Buffer *r = malloc(sizeof(Buffer));
    r->body = malloc(INIT_SIZE);
    r->nalloc = INIT_SIZE;
    r->len = 0;
    return r;
}

static void realloc_body(Buffer *b) {
    int newsize = b->nalloc * 2;
    char *body = malloc(newsize);
    memcpy(body, b->body, b->len);
    b->body = body;
    b->nalloc = newsize;
}

char *buf_body(Buffer *b) {
    return b->body;
}

int buf_len(Buffer *b) {
    return b->len;
}

void buf_write(Buffer *b, char c) {
    if (b->nalloc == (b->len + 1))
        realloc_body(b);
    b->body[b->len++] = c;
}

void buf_append(Buffer *b, char *s, int len) {
    for (int i = 0; i < len; i++)
        buf_write(b, s[i]);
}

void buf_printf(Buffer *b, char *fmt, ...) {
    va_list args;
    for (;;) {
        int avail = b->nalloc - b->len;
        va_start(args, fmt);
        int written = vsnprintf(b->body + b->len, avail, fmt, args);
        va_end(args);
        if (avail <= written) {
            realloc_body(b);
            continue;
        }
        b->len += written;
        return;
    }
}

char *vformat(char *fmt, va_list ap) {
    Buffer *b = make_buffer();
    va_list aq;
    for (;;) {
        int avail = b->nalloc - b->len;
        va_copy(aq, ap);
        int written = vsnprintf(b->body + b->len, avail, fmt, aq);
        va_end(aq);
        if (avail <= written) {
            realloc_body(b);
            continue;
        }
        b->len += written;
        return buf_body(b);
    }
}

char *format(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *r = vformat(fmt, ap);
    va_end(ap);
    return r;
}

static char *quote(char c) {
    switch (c) {
    case '"': return "\\\"";
    case '\\': return "\\\\";
    case '\b': return "\\b";
    case '\f': return "\\f";
    case '\n': return "\\n";
    case '\r': return "\\r";
    case '\t': return "\\t";
    }
    return NULL;
}

static void print(Buffer *b, char c) {
    char *q = quote(c);
    if (q) {
        buf_printf(b, "%s", q);
    } else if (isprint(c)) {
        buf_printf(b, "%c", c);
    } else {
        buf_printf(b, "\\x%02x", c);
    }
}

char *quote_cstring(char *p) {
    Buffer *b = make_buffer();
    while (*p)
        print(b, *p++);
    return buf_body(b);
}

char *quote_cstring_len(char *p, int len) {
    Buffer *b = make_buffer();
    for (int i = 0; i < len; i++)
        print(b, p[i]);
    return buf_body(b);
}

char *quote_char(char c) {
    if (c == '\\') return "\\\\";
    if (c == '\'') return "\\'";
    return format("%c", c);
}
