// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_VECTOR_H
#define EIGHTCC_VECTOR_H

#include <stdbool.h>

typedef struct Vector {
    void **body;
    int len;
    int nalloc;
} Vector;

#define EMPTY_VECTOR ((Vector){ .len = 0, .nalloc = 0 })

extern Vector *make_vector(void);
extern Vector *make_vector1(void *e);
extern Vector *vec_copy(Vector *vec);
extern void vec_push(Vector *vec, void *elem);
extern void *vec_pop(Vector *vec);
extern void vec_append(Vector *a, Vector *b);
extern void *vec_shift(Vector *vec);
extern void *vec_get(Vector *vec, int index);
extern void *vec_head(Vector *vec);
extern void *vec_tail(Vector *vec);
extern Vector *vec_reverse(Vector *vec);
extern void vec_clear(Vector *vec);
extern int vec_len(Vector *vec);

#endif
