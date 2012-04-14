// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

void test_bool(void) {
    _Bool v = 3;
    expect(1, v);
    v = 5;
    expect(1, v);
    v = 0.5;
    expect(1, v);
    v = 0.0;
    expect(0, v);
}

void testmain(void) {
    print("type conversion");
    test_bool();
}
