// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_MAP_H
#define EIGHTCC_MAP_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Map {
    struct Map *parent;
    char **key;
    void **val;
    int size;
    int nelem;
    int nused;
} Map;

#define EMPTY_MAP ((Map){})

extern Map *make_map(void);
extern Map *make_map_parent(Map *parent);
extern void *map_get(Map *map, char *key);
extern void map_put(Map *map, char *key, void *val);
extern void map_remove(Map *map, char *key);
extern size_t map_len(Map *map);

#endif
