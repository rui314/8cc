// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void test_signedcast() {
    unsigned char c = -1;
    int i = (signed char) c;

    expect(i, -1);
}

static void test_unsignedcast() {
    signed char c = -1;
    int i = (unsigned char) c;

    expect(1, i > 0);
}

void testmain() {
    print("cast");
    expectf(1, (int)1);
    expectf(1.0, (float)1);
    expectd(2.0, (double)2);

    int a[3];
    *(int *)(a + 2) = 5;
    expect(5, a[2]);

    test_signedcast();
    test_unsignedcast();
}
