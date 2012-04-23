// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

void test_basic(void) {
    expect(1, _Generic(5, int: 1, float: 2));
    expectd(3.0, _Generic(5.0, int: 1, float: 2.0, double: 3.0));
}

void test_default(void) {
    expect(1, _Generic(5, default: 1, float: 2));
    expectd(3.0, _Generic(5.0, int: 1, float: 2.0, default: 3.0));
}

void test_struct() {
    struct t1 { int x, y; } v1;
    struct t2 { int x, y, z; } v2;
    expect(10, _Generic(v1, struct t1: 10, struct t2: 11, default: 12));
    expect(11, _Generic(v2, struct t1: 10, struct t2: 11, default: 12));
    expect(12, _Generic(99, struct t1: 10, struct t2: 11, default: 12));
}

void test_array() {
    expect(20, _Generic("abc", char *: 20, default: 21));
    expect(22, _Generic((int[]){ 0 }, int *: 22, default: 23));
    expect(23, _Generic((int[]){ 0 }, int[]: 22, default: 23));
}

void testmain(void) {
    print("_Generic");
    test_basic();
    test_default();
    test_struct();
    test_array();
}
