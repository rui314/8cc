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

char *format(char *fmt, ...) {
    String *s = make_string();
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
        return get_cstring(s);
    }
}
