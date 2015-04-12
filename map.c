// Copyright 2014 Rui Ueyama. Released under the MIT license.

// This is an implementation of hash table.

#include <stdlib.h>
#include <string.h>
#include "8cc.h"

#define INIT_SIZE 16
#define TOMBSTONE ((void *)-1)

static uint32_t hash(char *p) {
    // FNV hash
    uint32_t r = 2166136261;
    for (; *p; p++) {
        r ^= *p;
        r *= 16777619;
    }
    return r;
}

static Map *do_make_map(Map *parent, int size) {
    Map *r = malloc(sizeof(Map));
    r->parent = parent;
    r->key = calloc(size, sizeof(char *));
    r->val = calloc(size, sizeof(void *));
    r->size = size;
    r->nelem = 0;
    r->nused = 0;
    return r;
}

static void maybe_rehash(Map *m) {
    if (!m->key) {
        m->key = calloc(INIT_SIZE, sizeof(char *));
        m->val = calloc(INIT_SIZE, sizeof(void *));
        m->size = INIT_SIZE;
        return;
    }
    if (m->nused < m->size * 0.7)
        return;
    int newsize = (m->nelem < m->size * 0.35) ? m->size : m->size * 2;
    char **k = calloc(newsize, sizeof(char *));
    void **v = calloc(newsize, sizeof(void *));
    int mask = newsize - 1;
    for (int i = 0; i < m->size; i++) {
        if (m->key[i] == NULL || m->key[i] == TOMBSTONE)
            continue;
        int j = hash(m->key[i]) & mask;
        for (;; j = (j + 1) & mask) {
            if (k[j] != NULL)
                continue;
            k[j] = m->key[i];
            v[j] = m->val[i];
            break;
        }
    }
    m->key = k;
    m->val = v;
    m->size = newsize;
    m->nused = m->nelem;
}

Map *make_map() {
    return do_make_map(NULL, INIT_SIZE);
}

Map *make_map_parent(Map *parent) {
    return do_make_map(parent, INIT_SIZE);
}

static void *map_get_nostack(Map *m, char *key) {
    if (!m->key)
        return NULL;
    int mask = m->size - 1;
    int i = hash(key) & mask;
    for (; m->key[i] != NULL; i = (i + 1) & mask)
        if (m->key[i] != TOMBSTONE && !strcmp(m->key[i], key))
            return m->val[i];
    return NULL;
}

void *map_get(Map *m, char *key) {
    void *r = map_get_nostack(m, key);
    if (r)
        return r;
    // Map is stackable. If no value is found,
    // continue searching from the parent.
    if (m->parent)
        return map_get(m->parent, key);
    return NULL;
}

void map_put(Map *m, char *key, void *val) {
    maybe_rehash(m);
    int mask = m->size - 1;
    int i = hash(key) & mask;
    for (;; i = (i + 1) & mask) {
        char *k = m->key[i];
        if (k == NULL || k == TOMBSTONE) {
            m->key[i] = key;
            m->val[i] = val;
            m->nelem++;
            if (k == NULL)
                m->nused++;
            return;
        }
        if (!strcmp(k, key)) {
            m->val[i] = val;
            return;
        }
    }
}

void map_remove(Map *m, char *key) {
    if (!m->key)
        return;
    int mask = m->size - 1;
    int i = hash(key) & mask;
    for (; m->key[i] != NULL; i = (i + 1) & mask) {
        if (m->key[i] == TOMBSTONE || strcmp(m->key[i], key))
            continue;
        m->key[i] = TOMBSTONE;
        m->val[i] = NULL;
        m->nelem--;
        return;
    }
}

size_t map_len(Map *m) {
    return m->nelem;
}
