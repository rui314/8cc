// Copyright 2014 Rui Ueyama. Released under the MIT license.

// Sets are containers that store unique strings.
//
// The data structure is functional. Because no destructive
// operation is defined, it's guranteed that a set will never
// change once it's created.
//
// A null pointer represents an empty set.
//
// Set is designed with simplicity in mind.
// It should be very fast for small number of items.
// However, if you plan to add a lot of items to a set,
// you should consider using Map as a set.

#include <stdlib.h>
#include <string.h>
#include "8cc.h"

Set *set_add(Set *s, char *v) {
    Set *r = malloc(sizeof(Set));
    r->next = s;
    r->v = v;
    return r;
}

bool set_has(Set *s, char *v) {
    for (; s; s = s->next)
        if (!strcmp(s->v, v))
            return true;
    return false;
}

Set *set_union(Set *a, Set *b) {
    Set *r = b;
    for (; a; a = a->next)
        if (!set_has(b, a->v))
            r = set_add(r, a->v);
    return r;
}

Set *set_intersection(Set *a, Set *b) {
    Set *r = NULL;
    for (; a; a = a->next)
        if (set_has(b, a->v))
            r = set_add(r, a->v);
    return r;
}
