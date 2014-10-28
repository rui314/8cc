// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <string.h>
#include <inttypes.h>
#include "8cc.h"

#define assert_true(expr) assert_true2(__LINE__, #expr, (expr))
#define assert_null(...) assert_null2(__LINE__, __VA_ARGS__)
#define assert_string(...) assert_string2(__LINE__, __VA_ARGS__)
#define assert_int(...) assert_int2(__LINE__, __VA_ARGS__)

static void assert_true2(int line, char *expr, int result) {
    if (!result)
        error("%d: assert_true: %s", line, expr);
}

static void assert_null2(int line, void *p) {
    if (p)
        error("%d: Null expected", line);
}

static void assert_string2(int line, char *s, char *t) {
    if (strcmp(s, t))
        error("%d: Expected \"%s\" but got \"%s\"", line, s, t);
}

static void assert_int2(int line, long a, long b) {
    if (a != b)
        error("%d: Expected %ld but got %ld", line, a, b);
}

static void test_string(void) {
    String *s = make_string();
    string_append(s, 'a');
    assert_string("a", get_cstring(s));
    string_append(s, 'b');
    assert_string("ab", get_cstring(s));

    string_appendf(s, ".");
    assert_string("ab.", get_cstring(s));
    string_appendf(s, "%s", "0123456789");
    assert_string("ab.0123456789", get_cstring(s));
}

static void test_list(void) {
    List *list = make_list();
    assert_int(0, list_len(list));
    list_push(list, (void *)1);
    assert_int(1, list_len(list));
    list_push(list, (void *)2);
    assert_int(2, list_len(list));

    Iter *iter = list_iter(list);
    assert_int(1, (long)iter_next(iter));
    assert_int(false, iter_end(iter));
    assert_int(2, (long)iter_next(iter));
    assert_int(true, iter_end(iter));
    assert_int(0, (long)iter_next(iter));
    assert_int(true, iter_end(iter));

    List *copy = list_copy(list);
    assert_int(2, list_len(copy));
    assert_int(1, (long)list_get(copy, 0));
    assert_int(2, (long)list_get(copy, 1));

    List *rev = list_reverse(list);
    iter = list_iter(rev);
    assert_int(2, (long)iter_next(iter));
    assert_int(1, (long)iter_next(iter));
    assert_int(0, (long)iter_next(iter));

    assert_int(2, list_len(rev));
    assert_int(1, (long)list_pop(rev));
    assert_int(1, list_len(rev));
    assert_int(2, (long)list_pop(rev));
    assert_int(0, list_len(rev));
    assert_int(0, (long)list_pop(rev));

    List *list2 = make_list();
    list_push(list2, (void *)5);
    list_push(list2, (void *)6);
    assert_int(5, (long)list_shift(list2));
    assert_int(6, (long)list_shift(list2));
    assert_int(0, (long)list_shift(list2));

    List *list3 = make_list();
    assert_int(0, (long)list_head(list3));
    assert_int(0, (long)list_tail(list3));
    list_push(list3, (void *)1);
    assert_int(1, (long)list_head(list3));
    assert_int(1, (long)list_tail(list3));
    list_push(list3, (void *)2);
    assert_int(1, (long)list_head(list3));
    assert_int(2, (long)list_tail(list3));

    List *list4 = make_list();
    list_push(list4, (void *)1);
    list_push(list4, (void *)2);
    assert_int(1, (long)list_get(list4, 0));
    assert_int(2, (long)list_get(list4, 1));
    assert_int(0, (long)list_get(list4, 2));
}

static void test_map(void) {
    Map *m = make_map();
    assert_null(map_get(m, "abc"));

    // Insert 10000 values
    for (int i = 0; i < 10000; i++) {
        char *k = format("%d", i);
        map_put(m, k, (void *)(intptr_t)i);
        assert_int(i, (int)(intptr_t)map_get(m, k));
    }

    // Insert again
    for (int i = 0; i < 1000; i++) {
        char *k = format("%d", i);
        map_put(m, k, (void *)(intptr_t)i);
        assert_int(i, (int)(intptr_t)map_get(m, k));
    }

    // Verify that the iterator iterates over all the elements
    {
	bool x[10000];
	for (int i = 0; i < 10000; i++)
	    x[i] = 0;
	MapIter *iter = map_iter(m);
	void *v;
	char *k = map_next(iter, &v);
	for (; k; k = map_next(iter, &v)) {
	    int i = (intptr_t)v;
	    x[i] = 1;
	}
	for (int i = 0; i < 10000; i++)
	    assert_true(x[i] == 1);
    }

    // Remove them
    for (int i = 0; i < 10000; i++) {
        char *k = format("%d", i);
        assert_int(i, (intptr_t)map_get(m, k));
        map_remove(m, k);
        assert_null(map_get(m, k));
    }
}

static void test_map_stack(void) {
    Map *m1 = make_map();
    map_put(m1, "x", (void *)1);
    map_put(m1, "y", (void *)2);
    assert_int(1, (int)(intptr_t)map_get(m1, "x"));

    Map *m2 = make_map_parent(m1);
    assert_int(1, (int)(intptr_t)map_get(m2, "x"));
    map_put(m2, "x", (void *)3);
    assert_int(3, (int)(intptr_t)map_get(m2, "x"));
    assert_int(1, (int)(intptr_t)map_get(m1, "x"));

    MapIter *iter = map_iter(m2);
    assert_string("x", map_next(iter, NULL));
    assert_string("y", map_next(iter, NULL));
    assert_null(map_next(iter, NULL));
}

static void test_dict(void) {
    Dict *dict = make_dict();
    assert_null(dict_get(dict, "abc"));
    dict_put(dict, "abc", (void *)50);
    dict_put(dict, "xyz", (void *)70);
    assert_int(50, (long)dict_get(dict, "abc"));
    assert_int(70, (long)dict_get(dict, "xyz"));
    assert_int(2, list_len(dict_keys(dict)));
}

int main(int argc, char **argv) {
    test_string();
    test_list();
    test_map();
    test_map_stack();
    test_dict();
    printf("Passed\n");
    return 0;
}
