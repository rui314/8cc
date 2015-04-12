// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void test_basic() {
    typeof(int) a = 5;
    expect(5, a);
    typeof(a) b = 6;
    expect(6, b);
}

static void test_array() {
    char a[] = "abc";
    typeof(a) b = "de";
    expect_string("de", b);
    expect(4, sizeof(b));

    typeof(typeof (char *)[4]) y;
    expect(4, sizeof(y) / sizeof(*y));

    typedef typeof(a[0]) CHAR;
    CHAR z = 'z';
    expect('z', z);
}

static void test_alt() {
    __typeof__(int) a = 10;
    expect(10, a);
}

void testmain() {
    print("typeof");
    test_basic();
    test_array();
    test_alt();
}
