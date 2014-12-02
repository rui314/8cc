// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_SET_H
#define EIGHTCC_SET_H

#include <stdbool.h>

typedef struct Set {
    char *v;
    struct Set *next;
} Set;

extern Set *set_add(Set *s, char *v);
extern bool set_has(Set *s, char *v);
extern Set *set_union(Set *a, Set *b);
extern Set *set_intersection(Set *a, Set *b);

#endif
