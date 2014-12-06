// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

/*
 * Vectors are containers of void pointers that can change in size.
 *
 * Note on memory management:
 *
 * No memory management is a memory management scheme in 8cc.
 * Memory regions allocated using malloc are never freed
 * until the process terminates. That has greatly simplified
 * the code and the API because 1) when you want to write
 * a function returning a new heap-allocated value, you can
 * just malloc a new value and return that, and 2) that design
 * decision has eliminated use-after-free bugs entirely.
 *
 * Modern computers have gigs of memory. 8cc consumes
 * only about 100MB to compile 10K lines of C source file.
 * Compiler is not a long-running process.
 * It will never run out of memory unless you give an
 * unrealistically large source file.
 *
 * If we really need to free memory, we could use Boehm garbage
 * collector. I don't see that need at this moment though.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "vector.h"

#define MIN_SIZE 8

static int max(int a, int b) {
    return a > b ? a : b;
}

static int roundup(int n) {
    if (n == 0)
        return 0;
    int r = 1;
    while (n > r)
        r *= 2;
    return r;
}

static Vector *do_make_vector(int size) {
    Vector *r = malloc(sizeof(Vector));
    size = roundup(size);
    if (size > 0)
        r->body = malloc(sizeof(void *) * size);
    r->len = 0;
    r->nalloc = size;
    return r;
}

Vector *make_vector(void) {
    return do_make_vector(0);
}

static void extend(Vector *vec, int delta) {
    if (vec->len + delta <= vec->nalloc)
        return;
    int nelem = max(roundup(vec->len + delta), MIN_SIZE);
    void *newbody = malloc(sizeof(void *) * nelem);
    memcpy(newbody, vec->body, sizeof(void *) * vec->len);
    vec->body = newbody;
    vec->nalloc = nelem;
}

Vector *make_vector1(void *e) {
    Vector *r = do_make_vector(0);
    vec_push(r, e);
    return r;
}

Vector *vec_copy(Vector *src) {
    Vector *r = do_make_vector(src->len);
    memcpy(r->body, src->body, sizeof(void *) * src->len);
    r->len = src->len;
    return r;
}

void vec_push(Vector *vec, void *elem) {
    extend(vec, 1);
    vec->body[vec->len++] = elem;
}

void vec_append(Vector *a, Vector *b) {
    extend(a, b->len);
    memcpy(a->body + a->len, b->body, sizeof(void *) * b->len);
    a->len += b->len;
}

void *vec_pop(Vector *vec) {
    assert(vec->len > 0);
    return vec->body[--vec->len];
}

void *vec_get(Vector *vec, int index) {
    assert(0 <= index && index < vec->len);
    return vec->body[index];
}

void vec_set(Vector *vec, int index, void *val) {
    assert(0 <= index && index < vec->len);
    vec->body[index] = val;
}

void *vec_head(Vector *vec) {
    assert(vec->len > 0);
    return vec->body[0];
}

void *vec_tail(Vector *vec) {
    assert(vec->len > 0);
    return vec->body[vec->len - 1];
}

Vector *vec_reverse(Vector *vec) {
    Vector *r = do_make_vector(vec->len);
    for (int i = 0; i < vec->len; i++)
        r->body[i] = vec->body[vec->len - i - 1];
    r->len = vec->len;
    return r;
}

void *vec_body(Vector *vec) {
    return vec->body;
}

int vec_len(Vector *vec) {
    return vec->len;
}
