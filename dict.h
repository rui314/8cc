// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_DICT_H
#define EIGHTCC_DICT_H

#include "vector.h"
#include "map.h"

typedef struct Dict {
    Map *map;
    Vector *key;
} Dict;

Dict *make_dict(void);
void *dict_get(Dict *dict, char *key);
void dict_put(Dict *dict, char *key, void *val);
Vector *dict_keys(Dict *dict);

#endif
