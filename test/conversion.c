// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

static void test_bool(void) {
    _Bool v = 3;
    expect(1, v);
    v = 5;
    expect(1, v);
    v = 0.5;
    expect(1, v);
    v = 0.0;
    expect(0, v);
}

static void test_float(void) {
    double a = 4.0;
    float b = a;
    expectf(4, b);
}

void testmain(void) {
    print("type conversion");
    test_bool();
    test_float();
}
