// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

static void t1(void) {
    int a = 61;
    int *b = &a;
    expect(61, *b);
}

static void t2(void) {
    char *c = "ab";
    expect(97, *c);
}

static void t3(void) {
    char *c = "ab" + 1;
    expect(98, *c);
}

static void t4(void) {
    char s[] = "xyz";
    char *c = s + 2;
    expect(122, *c);
}

static void t5(void) {
    char s[] = "xyz";
    *s = 65;
    expect(65, *s);
}

static void t6(void) {
    struct tag {
        int val;
        struct tag *next;
    };
    struct tag node1 = { 1, NULL };
    struct tag node2 = { 2, &node1 };
    struct tag node3 = { 3, &node2 };
    struct tag *p = &node3;
    expect(3, p->val);
    expect(2, p->next->val);
    expect(1, p->next->next->val);
    p->next = p->next->next;
    expect(1, p->next->val);
}

static void subtract(void) {
    char *p = "abcdefg";
    char *q = p + 5;
    expect(5, q - p);
}

void testmain(void) {
    print("pointer");
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    subtract();
}
