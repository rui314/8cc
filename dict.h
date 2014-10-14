// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_DICT_H
#define EIGHTCC_DICT_H

#include "list.h"

typedef struct Dict {
    List *list;
    struct Dict *parent;
    int size;
} Dict;

#define EMPTY_DICT                              \
    ((Dict){ &EMPTY_LIST, NULL })

void *make_dict(void *parent);
void *dict_get(Dict *dict, char *key);
void dict_put(Dict *dict, char *key, void *val);
void dict_remove(Dict *dict, char *key);
bool dict_empty(Dict *dict);
List *dict_keys(Dict *dict);
List *dict_values(Dict *dict);
void *dict_parent(Dict *dict);

#endif
