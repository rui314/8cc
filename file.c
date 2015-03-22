// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

/*
 * This file provides character input stream for C source code.
 * An input stream is either backed by stdio's FILE * or
 * backed by a string.
 * The following input processing is done at this stage.
 *
 * - C11 5.1.1.2p1: "\r\n" or "\r" are canonicalized to "\n".
 * - C11 5.1.1.2p2: A sequence of backslash and newline is removed.
 * - EOF not immediately following a newline is converted to
 *   a sequence of newline and EOF. (The C spec requires source
 *   files end in a newline character (5.1.1.2p2). Thus, if all
 *   source files are comforming, this step wouldn't be needed.)
 *
 * Trigraphs are not supported by design.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "8cc.h"

static Vector *files = &EMPTY_VECTOR;
static Vector *stashed = &EMPTY_VECTOR;

File *make_file(FILE *file, char *name) {
    File *r = calloc(1, sizeof(File));
    r->file = file;
    r->name = name;
    r->line = 1;
    return r;
}

File *make_file_string(char *s) {
    File *r = calloc(1, sizeof(File));
    r->line = 1;
    r->p = s;
    return r;
}

static void close_file(File *f) {
    if (f->file)
        fclose(f->file);
}

static int readc_file(File *f) {
    int c = getc(f->file);
    if (c == EOF) {
        c = (f->last == '\n' || f->last == EOF) ? EOF : '\n';
    } else if (c == '\r') {
        int c2 = getc(f->file);
        if (c2 != '\n')
            ungetc(c2, f->file);
        c = '\n';
    }
    f->last = c;
    return c;
}

static int readc_string(File *f) {
    int c;
    if (*f->p == '\0') {
        c = (f->last == '\n' || f->last == EOF) ? EOF : '\n';
    } else if (*f->p == '\r') {
        f->p++;
        if (*f->p == '\n')
            f->p++;
        c = '\n';
    } else {
        c = *f->p++;
    }
    f->last = c;
    return c;
}

static int get(void) {
    File *f = vec_tail(files);
    int c;
    if (f->buflen > 0) {
        c = f->buf[--f->buflen];
    } else if (f->file) {
        c = readc_file(f);
    } else {
        c = readc_string(f);
    }
    if (c == '\n') {
        f->line++;
        f->column = 0;
    } else {
        f->column++;
    }
    return c;
}

int readc(void) {
    for (;;) {
        int c = get();
        if (c == EOF) {
            if (vec_len(files) == 1)
                return c;
            close_file(vec_pop(files));
            continue;
        }
        if (c != '\\')
            return c;
        int c2 = get();
        if (c2 == '\n')
            continue;
        unreadc(c2);
        return c;
    }
}

void unreadc(int c) {
    if (c == EOF)
        return;
    File *f = vec_tail(files);
    assert(f->buflen < sizeof(f->buf) / sizeof(f->buf[0]));
    f->buf[f->buflen++] = c;
    if (c == '\n') {
        f->column = 0;
        f->line--;
    } else {
        f->column--;
    }
}

File *current_file(void) {
    return vec_tail(files);
}

void stream_push(File *f) {
    vec_push(files, f);
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

void stream_stash(File *f) {
    vec_push(stashed, files);
    files = make_vector1(f);
}

void stream_unstash(void) {
    files = vec_pop(stashed);
}
