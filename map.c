// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

// This is an implementation of hash table. Unlike Dict,
// the order of values inserted to a hash table is not preserved.
// Map should be faster than Dict.

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "map.h"

#define INIT_SIZE 16

static Bucket *make_bucket(char *key, void *val, void *next) {
    Bucket *b = malloc(sizeof(Bucket));
    b->key = key;
    b->val = val;
    b->next = next;
    return b;
}

static uint32_t hash(char *p) {
    // FNV hash
    uint32_t r = 2166136261;
    for (; *p; p++) {
        r ^= *p;
        r *= 16777619;
    }
    return r;
}

static Map *do_make_map(Map *parent, int cap) {
    Map *r = malloc(sizeof(Map));
    r->parent = parent;
    r->buckets = malloc(sizeof(Bucket) * cap);
    for (int i = 0; i < cap; i++)
        r->buckets[i] = NULL;
    r->nelem = 0;
    r->cap = cap;
    return r;
}

static void maybe_rehash(Map *map) {
    if (!map->buckets) {
        *map = *do_make_map(NULL, INIT_SIZE);
        return;
    }
    if (map->nelem * 3 < map->cap)
        return;
    Map *m = do_make_map(map->parent, map->cap * 2);
    for (int i = 0; i < map->cap; i++) {
        Bucket *b = map->buckets[i];
        for (; b; b = b->next)
            map_put(m, b->key, b->val);
    }
    *map = *m;
}

Map *make_map(Map *parent) {
    return do_make_map(parent, INIT_SIZE);
}

void *map_get(Map *map, char *key) {
    if (!map->buckets)
        return NULL;
    uint32_t h = hash(key);
    Bucket *b = map->buckets[h % map->cap];
    for (; b; b = b->next)
        if (!strcmp(b->key, key))
            return b->val;
    // Map is stackable; if no value is found,
    // continue searching from the parent.
    if (map->parent)
	return map_get(map->parent, key);
    return NULL;
}

void map_put(Map *map, char *key, void *val) {
    maybe_rehash(map);
    uint32_t h = hash(key);
    int idx = h % map->cap;
    Bucket *b = map->buckets[idx];
    for (; b; b = b->next) {
        if (!strcmp(b->key, key)) {
            b->val = val;
            return;
        }
    }
    map->buckets[idx] = make_bucket(key, val, map->buckets[idx]);
    map->nelem++;
}

void map_remove(Map *map, char *key) {
    if (!map->buckets)
        return;
    uint32_t h = hash(key);
    int idx = h % map->cap;
    Bucket *b = map->buckets[idx];
    Bucket **prev = &map->buckets[idx];
    for (; b; prev = &(*prev)->next, b = b->next) {
        if (!strcmp(b->key, key)) {
            *prev = b->next;
            map->nelem--;
            return;
        }
    }
}

size_t map_size(Map *map) {
  return map->nelem;
}

MapIter *map_iter(Map *map) {
    MapIter *r = malloc(sizeof(MapIter));
    r->map = map;
    r->bucket = NULL;
    r->i = 0;
    return r;
}

char *map_next(MapIter *iter) {
    if (iter->bucket && iter->bucket->next) {
        iter->bucket = iter->bucket->next;
        return iter->bucket->key;
    }
    while (iter->i < iter->map->cap) {
        Bucket *b = iter->map->buckets[iter->i];
        iter->i++;
        if (b) {
            iter->bucket = b;
            return b->key;
        }
    }
    return NULL;
}
