// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"
#include <stdbool.h>

static void test_primitives(void) {
    expect(1, sizeof(void));
    expect(1, sizeof(test_primitives));
    expect(1, sizeof(char));
    expect(1, sizeof(_Bool));
    expect(1, sizeof(bool));
    expect(2, sizeof(short));
    expect(4, sizeof(int));
    expect(8, sizeof(long));
}

static void test_pointers(void) {
    expect(8, sizeof(char *));
    expect(8, sizeof(short *));
    expect(8, sizeof(int *));
    expect(8, sizeof(long *));
}

static void test_unsigned(void) {
    expect(1, sizeof(unsigned char));
    expect(2, sizeof(unsigned short));
    expect(4, sizeof(unsigned int));
    expect(8, sizeof(unsigned long));
}

static void test_literals(void) {
    expect(4, sizeof 1);
    expect(4, sizeof('a'));
    expect(4, sizeof(1.0f));
    expect(8, sizeof 1L);
    expect(8, sizeof 1.0);
    expect(8, sizeof(1.0));
}

static void test_arrays(void) {
    expect(1, sizeof(char[1]));
    expect(7, sizeof(char[7]));
    expect(30, sizeof(char[3][10]));
    expect(32, sizeof(int[4][2]));
}

static void test_vars(void) {
    char a[] = { 1, 2, 3 };
    expect(3, sizeof(a));
    char b[] = "abc";
    expect(4, sizeof(b));
    expect(1, sizeof(b[0]));
    expect(1, sizeof((b[0])));
    expect(1, sizeof((b)[0]));
    char *c[5];
    expect(40, sizeof(c));
    char *(*d)[3];
    expect(8, sizeof(d));
    expect(24, sizeof(*d));
    expect(8, sizeof(**d));
    expect(1, sizeof(***d));
    expect(4, sizeof((int)a));
}

static void test_struct(void) {
    expect(1, sizeof(struct { char a; }));
    expect(3, sizeof(struct { char a[3]; }));
    expect(5, sizeof(struct { char a[5]; }));
    expect(8, sizeof(struct { int a; char b; }));
    expect(12, sizeof(struct { char a; int b; char c; }));
    expect(24, sizeof(struct { char a; double b; char c; }));
    expect(72, sizeof(struct { char a; double b; char c; }[3]));
    expect(24, sizeof(struct { struct { char a; double b; } x; char c; }));
}

void testmain(void) {
    print("sizeof");
    test_primitives();
    test_pointers();
    test_unsigned();
    test_literals();
    test_arrays();
    test_vars();
    test_struct();
}
