// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

// This is an implementation of hash table.

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "map.h"

#define INIT_SIZE 16

typedef struct Bucket {
    char *key;
    void *val;
    struct Bucket *next;
} Bucket;

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
    r->buckets = malloc(sizeof(r->buckets[0]) * cap);
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

Map *make_map(void) {
    return do_make_map(NULL, INIT_SIZE);
}

Map *make_map_parent(Map *parent) {
    return do_make_map(parent, INIT_SIZE);
}

static void *map_get_nostack(Map *map, char *key) {
    if (!map->buckets)
        return NULL;
    uint32_t h = hash(key);
    Bucket *b = map->buckets[h % map->cap];
    for (; b; b = b->next)
        if (!strcmp(b->key, key))
            return b->val;
    return NULL;
}

void *map_get(Map *map, char *key) {
    void *r = map_get_nostack(map, key);
    if (r)
        return r;
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

size_t map_len(Map *map) {
  return map->nelem;
}

MapIter *map_iter(Map *map) {
    MapIter *r = malloc(sizeof(MapIter));
    r->map = map;
    r->cur = map;
    r->bucket = NULL;
    r->i = 0;
    return r;
}

static char *do_map_next(MapIter *iter, void **val) {
    if (iter->bucket && iter->bucket->next) {
        iter->bucket = iter->bucket->next;
        if (val)
            *val = iter->bucket->val;
        return iter->bucket->key;
    }
    while (iter->i < iter->cur->cap) {
        Bucket *b = iter->cur->buckets[iter->i];
        iter->i++;
        if (b) {
            iter->bucket = b;
            if (val)
                *val = b->val;
            return b->key;
        }
    }
    return NULL;
}

static bool is_dup(MapIter *iter, char *k) {
    for (Map *p = iter->map; p != iter->cur; p = p->parent)
        if (map_get_nostack(p, k))
            return true;
    return false;
}

char *map_next(MapIter *iter, void **val) {
    if (!iter->cur)
        return NULL;
    for (;;) {
        char *k = do_map_next(iter, val);
        if (!k)
            break;
        if (is_dup(iter, k))
            continue;
        return k;
    }
    iter->cur = iter->cur->parent;
    if (iter->cur) {
        iter->bucket = NULL;
        iter->i = 0;
        return map_next(iter, val);
    }
    return NULL;
}
