// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

void test_basic(void) {
    typeof(int) a = 5;
    expect(5, a);
    typeof(a) b = 6;
    expect(6, b);
}

void test_array(void) {
    char a[] = "abc";
    typeof(a) b = "de";
    expect_string("de", b);
    expect(4, sizeof(b));

    typeof(typeof (char *)[4]) y;
    expect(4, sizeof(y) / sizeof(*y));
}

void test_alt(void) {
    __typeof__(int) a = 10;
    expect(10, a);
}

void testmain(void) {
    print("typeof");
    test_basic();
    test_array();
    test_alt();
}
