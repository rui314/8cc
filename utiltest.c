// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <string.h>
#include "8cc.h"

char *get_base_file(void) { return NULL; }

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

static void test_buf() {
    Buffer *b = make_buffer();
    buf_write(b, 'a');
    buf_write(b, 'b');
    buf_write(b, '\0');
    assert_string("ab", buf_body(b));

    Buffer *b2 = make_buffer();
    buf_write(b2, '.');
    buf_printf(b2, "%s", "0123456789");
    assert_string(".0123456789", buf_body(b2));
}

static void test_list() {
    Vector *list = make_vector();
    assert_int(0, vec_len(list));
    vec_push(list, (void *)1);
    assert_int(1, vec_len(list));
    vec_push(list, (void *)2);
    assert_int(2, vec_len(list));

    Vector *copy = vec_copy(list);
    assert_int(2, vec_len(copy));
    assert_int(1, (long)vec_get(copy, 0));
    assert_int(2, (long)vec_get(copy, 1));

    Vector *rev = vec_reverse(list);
    assert_int(2, vec_len(rev));
    assert_int(1, (long)vec_pop(rev));
    assert_int(1, vec_len(rev));
    assert_int(2, (long)vec_pop(rev));
    assert_int(0, vec_len(rev));

    Vector *list3 = make_vector();
    vec_push(list3, (void *)1);
    assert_int(1, (long)vec_head(list3));
    assert_int(1, (long)vec_tail(list3));
    vec_push(list3, (void *)2);
    assert_int(1, (long)vec_head(list3));
    assert_int(2, (long)vec_tail(list3));

    Vector *list4 = make_vector();
    vec_push(list4, (void *)1);
    vec_push(list4, (void *)2);
    assert_int(1, (long)vec_get(list4, 0));
    assert_int(2, (long)vec_get(list4, 1));
}

static void test_map() {
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

    // Remove them
    for (int i = 0; i < 10000; i++) {
        char *k = format("%d", i);
        assert_int(i, (intptr_t)map_get(m, k));
        map_remove(m, k);
        assert_null(map_get(m, k));
    }
}

static void test_map_stack() {
    Map *m1 = make_map();
    map_put(m1, "x", (void *)1);
    map_put(m1, "y", (void *)2);
    assert_int(1, (int)(intptr_t)map_get(m1, "x"));

    Map *m2 = make_map_parent(m1);
    assert_int(1, (int)(intptr_t)map_get(m2, "x"));
    map_put(m2, "x", (void *)3);
    assert_int(3, (int)(intptr_t)map_get(m2, "x"));
    assert_int(1, (int)(intptr_t)map_get(m1, "x"));
}

static void test_dict() {
    Dict *dict = make_dict();
    assert_null(dict_get(dict, "abc"));
    dict_put(dict, "abc", (void *)50);
    dict_put(dict, "xyz", (void *)70);
    assert_int(50, (long)dict_get(dict, "abc"));
    assert_int(70, (long)dict_get(dict, "xyz"));
    assert_int(2, vec_len(dict_keys(dict)));
}

static void test_set() {
    Set *s = NULL;
    assert_int(0, set_has(s, "abc"));
    s = set_add(s, "abc");
    s = set_add(s, "def");
    assert_int(1, set_has(s, "abc"));
    assert_int(1, set_has(s, "def"));
    assert_int(0, set_has(s, "xyz"));
    Set *t = NULL;
    t = set_add(t, "abc");
    t = set_add(t, "DEF");
    assert_int(1, set_has(set_union(s, t), "abc"));
    assert_int(1, set_has(set_union(s, t), "def"));
    assert_int(1, set_has(set_union(s, t), "DEF"));
    assert_int(1, set_has(set_intersection(s, t), "abc"));
    assert_int(0, set_has(set_intersection(s, t), "def"));
    assert_int(0, set_has(set_intersection(s, t), "DEF"));
}

static void test_path() {
    assert_string("/abc", fullpath("/abc"));
    assert_string("/abc/def", fullpath("/abc/def"));
    assert_string("/abc/def", fullpath("/abc///def"));
    assert_string("/abc/def", fullpath("//abc///def"));
    assert_string("/abc/xyz", fullpath("/abc/def/../xyz"));
    assert_string("/xyz", fullpath("/abc/def/../../../xyz"));
    assert_string("/xyz", fullpath("/abc/def/../../../../xyz"));
}

static void test_file() {
    stream_push(make_file_string("abc"));
    assert_int('a', readc());
    assert_int('b', readc());
    unreadc('b');
    unreadc('a');
    assert_int('a', readc());
    assert_int('b', readc());
    assert_int('c', readc());
    assert_int('\n', readc());
    unreadc('\n');
    assert_int('\n', readc());
    assert_true(readc() < 0);
}

int main(int argc, char **argv) {
    test_buf();
    test_list();
    test_map();
    test_map_stack();
    test_dict();
    test_set();
    test_path();
    test_file();
    printf("Passed\n");
    return 0;
}
