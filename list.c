// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "error.h"

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

static List *do_make_list(int size) {
    List *r = malloc(sizeof(List));
    size = roundup(size);
    if (size > 0)
        r->body = malloc(sizeof(void *) * size);
    r->len = 0;
    r->nalloc = size;
    return r;
}

List *make_list(void) {
    return do_make_list(0);
}

static void extend(List *list, int delta) {
    if (list->len + delta <= list->nalloc)
        return;
    int nelem = max(roundup(list->len + delta), MIN_SIZE);
    void *newbody = malloc(sizeof(void *) * nelem);
    memcpy(newbody, list->body, sizeof(void *) * list->len);
    list->body = newbody;
    list->nalloc = nelem;
}

List *make_list1(void *e) {
    List *r = do_make_list(0);
    list_push(r, e);
    return r;
}

List *list_copy(List *src) {
    List *r = do_make_list(src->len);
    for (int i = 0; i < src->len; i++)
        r->body[i] = src->body[i];
    r->len = src->len;
    return r;
}

void list_push(List *list, void *elem) {
    extend(list, 1);
    list->body[list->len++] = elem;
}

void list_append(List *a, List *b) {
    extend(a, b->len);
    int i = a->len;
    int j = 0;
    while (j < b->len) {
        a->body[i] = b->body[j];
        i++;
        j++;
    }
    a->len += b->len;
}

void *list_pop(List *list) {
    if (list->len == 0)
        return NULL;
    return list->body[--list->len];
}

void *list_shift(List *list) {
    if (list->len == 0)
        return NULL;
    void *r = list->body[0];
    for (int i = 1; i < list->len; i++)
        list->body[i - 1] = list->body[i];
    list->len--;
    return r;
}

void list_unshift(List *list, void *elem) {
    extend(list, 1);
    for (int i = 0; i < list->len; i++)
        list->body[i] = list->body[i + 1];
    list->body[0] = elem;
    list->len++;
}

void *list_get(List *list, int index) {
    if (index < 0 || list->len <= index)
        return NULL;
    return list->body[index];
}

void *list_head(List *list) {
    return (list->len == 0) ? NULL : list->body[0];
}

void *list_tail(List *list) {
    return (list->len == 0) ? NULL : list->body[list->len - 1];
}

List *list_reverse(List *list) {
    List *r = do_make_list(list->len);
    int i = 0;
    int j = list->len - 1;
    while (i < list->len) {
        r->body[i] = list->body[j];
        i++;
        j--;
    }
    r->len = list->len;
    return r;
}

void list_clear(List *list) {
    list->len = 0;
}

int list_len(List *list) {
    return list->len;
}

Iter *list_iter(List *list) {
    Iter *r = malloc(sizeof(Iter));
    r->list = list;
    r->i = 0;
    return r;
}

void *iter_next(Iter *iter) {
    if (iter->i >= iter->list->len)
        return NULL;
    return iter->list->body[iter->i++];
}

bool iter_end(Iter *iter) {
    return iter->i >= iter->list->len;
}
