// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void verify(int *expected, int *got, int len) {
    for (int i = 0; i < len; i++)
        expect(expected[i], got[i]);
}

static void verify_short(short *expected, short *got, int len) {
    for (int i = 0; i < len; i++)
        expect(expected[i], got[i]);
}

static void test_array() {
    int x[] = { 1, 3, 5 };
    expect(1, x[0]);
    expect(3, x[1]);
    expect(5, x[2]);

    int ye[] = { 1, 3, 5, 2, 4, 6, 3, 5, 7, 0, 0, 0 };
    int y1[4][3] = { { 1, 3, 5 }, { 2, 4, 6 }, { 3, 5, 7 }, };
    verify(ye, y1, 12);
    int y2[4][3] = { 1, 3, 5, 2, 4, 6, 3, 5, 7 };
    verify(ye, y2, 12);

    int ze[] = { 1, 0, 0, 2, 0, 0, 3, 0, 0, 4, 0, 0 };
    int z[4][3] = { { 1 }, { 2 }, { 3 }, { 4 } };
    verify(ze, z, 12);

    short qe[24] = { 1, 0, 0, 0, 0, 0, 2, 3, 0, 0, 0, 0, 4, 5, 6 };
    short q[4][3][2] = { { 1 }, { 2, 3 }, { 4, 5, 6 } };
    verify_short(qe, q, 24);

    int a[] = {{{ 3 }}};
    expect(3, a[0]);
}

static void test_string() {
    char s[] = "abc";
    expect_string("abc", s);
    char t[] = { "def" };
    expect_string("def", t);
}

static void test_struct() {
    int we[] = { 1, 0, 0, 0, 2, 0, 0, 0 };
    struct { int a[3]; int b; } w[] = { { 1 }, 2 };
    verify(we, &w, 8);
}

static void test_primitive() {
    int a = { 59 };
    expect(59, a);
}

static void test_nested() {
    struct {
        struct {
            struct { int a; int b; } x;
            struct { char c[8]; } y;
        } w;
    } v = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, };
    expect(1, v.w.x.a);
    expect(2, v.w.x.b);
    expect(3, v.w.y.c[0]);
    expect(10, v.w.y.c[7]);
}

static void test_array_designator() {
    int v[3] = { [1] = 5 };
    expect(0, v[0]);
    expect(5, v[1]);
    expect(0, v[2]);

    struct { int a, b; } x[2] = { [1] = { 1, 2 } };
    expect(0, x[0].a);
    expect(0, x[0].b);
    expect(1, x[1].a);
    expect(2, x[1].b);

    struct { int a, b; } x2[3] = { [1] = 1, 2, 3, 4 };
    expect(0, x2[0].a);
    expect(0, x2[0].b);
    expect(1, x2[1].a);
    expect(2, x2[1].b);
    expect(3, x2[2].a);
    expect(4, x2[2].b);

    int x3[] = { [2] = 3, [0] = 1, 2 };
    expect(1, x3[0]);
    expect(2, x3[1]);
    expect(3, x3[2]);
}

static void test_struct_designator() {
    struct { int x; int y; } v1 = { .y = 1, .x = 5 };
    expect(5, v1.x);
    expect(1, v1.y);

    struct { int x; int y; } v2 = { .y = 7 };
    expect(7, v2.y);

    struct { int x; int y; int z; } v3 = { .y = 12, 17 };
    expect(12, v3.y);
    expect(17, v3.z);
}

static void test_complex_designator() {
    struct { struct { int a, b; } x[3]; } y[] = {
        [1].x[0].b = 5, 6, 7, 8, 9,
        [0].x[2].b = 10, 11
    };
    expect(0, y[0].x[0].a);
    expect(0, y[0].x[0].b);
    expect(0, y[0].x[1].a);
    expect(0, y[0].x[1].b);
    expect(0, y[0].x[2].a);
    expect(10, y[0].x[2].b);
    expect(11, y[1].x[0].a);
    expect(5, y[1].x[0].b);
    expect(6, y[1].x[1].a);
    expect(7, y[1].x[1].b);
    expect(8, y[1].x[2].a);
    expect(9, y[1].x[2].b);

    int y2[][3] = { [0][0] = 1, [1][0] = 3 };
    expect(1, y2[0][0]);
    expect(3, y2[1][0]);

    struct { int a, b[3]; } y3 = { .a = 1, .b[0] = 10, .b[1] = 11 };
    expect(1, y3.a);
    expect(10, y3.b[0]);
    expect(11, y3.b[1]);
    expect(0, y3.b[2]);
}

static void test_zero() {
    struct tag { int x, y; };
    struct tag v0 = (struct tag){ 6 };
    expect(6, v0.x);
    expect(0, v0.y);

    struct { int x; int y; } v1 = { 6 };
    expect(6, v1.x);
    expect(0, v1.y);

    struct { int x; int y; } v2 = { .y = 3 };
    expect(0, v2.x);
    expect(3, v2.y);

    struct { union { int x, y; }; } v3 = { .x = 61 };
    expect(61, v3.x);
}


static void test_typedef() {
    typedef int A[];
    A a = { 1, 2 };
    A b = { 3, 4, 5 };
    expect(2, sizeof(a) / sizeof(*a));
    expect(3, sizeof(b) / sizeof(*b));
}

static void test_excessive() {
#ifdef __8cc__
#pragma disable_warning
#endif

    char x1[3] = { 1, 2, 3, 4, 5 };
    expect(3, sizeof(x1));

    char x2[3] = "abcdefg";
    expect(3, sizeof(x2));
    expect(0, strncmp("abc", x2, 3));

#ifdef __8cc__
#pragma disable_warning
#endif
}

void testmain() {
    print("initializer");

    test_array();
    test_string();
    test_struct();
    test_primitive();
    test_nested();
    test_array_designator();
    test_struct_designator();
    test_complex_designator();
    test_zero();
    test_typedef();
    test_excessive();
}
