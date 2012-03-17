#include <string.h>
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
        error("%d: Expected %s but got %s", line, s, t);
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

    assert_int(2, (long)list_last(list));

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
}

static void test_dict(void) {
    Dict *dict = make_dict(NULL);
    assert_null(dict_parent(dict));
    assert_null(dict_get(dict, "abc"));
    dict_put(dict, "abc", (void *)50);
    dict_put(dict, "xyz", (void *)70);
    assert_int(50, (long)dict_get(dict, "abc"));
    assert_int(70, (long)dict_get(dict, "xyz"));

    Dict *dict2 = make_dict(dict);
    assert_true(dict_parent(dict2) == dict);
    assert_int(50, (long)dict_get(dict, "abc"));
    assert_int(70, (long)dict_get(dict, "xyz"));
    dict_put(dict2, "ABC", (void *)110);
    assert_int(110, (long)dict_get(dict2, "ABC"));
    assert_null(dict_get(dict, "ABC"));

    assert_int(3, list_len(dict_values(dict2)));
    assert_int(2, list_len(dict_values(dict)));
    assert_int(3, list_len(dict_keys(dict2)));
    assert_int(2, list_len(dict_keys(dict)));

    Dict *dict3 = make_dict(NULL);
    dict_put(dict3, "abc", (void *)10);
    assert_int(10, (long)dict_get(dict3, "abc"));
    dict_remove(dict3, "abc");
    assert_int(0, (long)dict_get(dict3, "abc"));
}

int main(int argc, char **argv) {
    test_string();
    test_list();
    test_dict();
    printf("Passed\n");
    return 0;
}
