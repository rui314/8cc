// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "8cc.h"

static Vector *files = &EMPTY_VECTOR;

static File *make_file(FILE *file, char *name, bool autopop) {
    File *r = calloc(1, sizeof(File));
    r->file = file;
    r->name = name;
    r->line = 1;
    r->autopop = autopop;
    return r;
}

static File *make_file_string(char *s, bool autopop) {
    File *r = calloc(1, sizeof(File));
    r->line = 1;
    r->autopop = autopop;
    r->p = s;
    return r;
}

static int readc_file(File *f) {
    int c = (f->buflen > 0) ? f->buf[--f->buflen] : getc(f->file);
    if (c == EOF)
        return c;
    if (c != '\r' && c != '\n') {
        f->column++;
        return c;
    }
    if (c == '\r') {
        int c2 = getc(f->file);
        if (c2 != '\n')
            ungetc(c2, f->file);
    }
    f->line++;
    f->column = 0;
    return '\n';
}

static int readc_string(File *f) {
    if (*f->p == '\0')
        return EOF;
    if (*f->p == '\n') {
        f->line++;
        f->column = 0;
    } else {
        f->column++;
    }
    return *f->p++;
}

int readc(void) {
    File *f = vec_tail(files);
    int c = f->file ? readc_file(f) : readc_string(f);
    if (c != EOF)
        return c;
    if (vec_len(files) == 0)
        return EOF;
    if (f->autopop) {
        vec_pop(files);
        return '\n';
    }
    return EOF;
}

void unreadc(int c) {
    if (c < 0)
        return;
    File *f = vec_tail(files);
    if (c == '\n') {
        f->column = 0;
        f->line--;
    } else {
        f->column--;
    }
    if (f->file) {
        assert(f->buflen < sizeof(f->buf) / sizeof(f->buf[0]));
        f->buf[f->buflen++] = c;
        return;
    }
    assert(f->p[-1] == c);
    f->p--;
}

File *current_file(void) {
    return vec_tail(files);
}

void insert_stream(FILE *file, char *name) {
    vec_push(files, make_file(file, name, true));
}

void push_stream(FILE *file, char *name) {
    vec_push(files, make_file(file, name, false));
}

void push_stream_string(char *s) {
    vec_push(files, make_file_string(s, false));
}

void pop_stream(void) {
    vec_pop(files);
}

int stream_depth(void) {
    return vec_len(files);
}

char *input_position(void) {
    if (vec_len(files) == 0)
        return "(unknown)";
    File *f = vec_tail(files);
    return format("%s:%d:%d", f->name, f->line, f->column);
}
