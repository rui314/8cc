// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void t1() {
    int a = 61;
    int *b = &a;
    expect(61, *b);
}

static void t2() {
    char *c = "ab";
    expect(97, *c);
}

static void t3() {
    char *c = "ab" + 1;
    expect(98, *c);
}

static void t4() {
    char s[] = "xyz";
    char *c = s + 2;
    expect(122, *c);
}

static void t5() {
    char s[] = "xyz";
    *s = 65;
    expect(65, *s);
}

static void t6() {
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

static void t7() {
    int a;
    int *p1 = &a + 1;
    int *p2 = 1 + &a;
    expect(0, p1 - p2);
}

static void subtract() {
    char *p = "abcdefg";
    char *q = p + 5;
    expect(8, sizeof(q - p));
    expect(5, q - p);
}

static void compare() {
    char *p = "abcdefg";
    expect(0, p == p + 1);
    expect(1, p == p);
    expect(0, p != p);
    expect(1, p != p + 1);
    expect(0, p < p);
    expect(1, p < p + 1);
    expect(0, p > p);
    expect(1, p + 1 > p);
    expect(1, p >= p);
    expect(1, p + 1 >= p);
    expect(0, p >= p + 1);
    expect(1, p <= p);
    expect(1, p <= p + 1);
    expect(0, p + 1 <= p);
    expect(4, sizeof(p == p + 1));
    expect(4, sizeof(p != p + 1));
    expect(4, sizeof(p < p + 1));
    expect(4, sizeof(p > p + 1));
    expect(4, sizeof(p <= p + 1));
    expect(4, sizeof(p >= p + 1));
}

void testmain() {
    print("pointer");
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    t7();
    subtract();
    compare();
}
