// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void test_bool() {
    _Bool v = 3;
    expect(1, v);
    v = 5;
    expect(1, v);
    v = 0.5;
    expect(1, v);
    v = 0.0;
    expect(0, v);
}

static void test_float() {
    double a = 4.0;
    float b = a;
    expectf(4, b);
}

void testmain() {
    print("type conversion");
    test_bool();
    test_float();
}
