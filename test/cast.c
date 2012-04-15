// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

void testmain(void) {
    print("cast");
    expectf(1, (int)1);
    expectf(1.0, (float)1);
    expectd(2.0, (double)2);

    int a[3];
    *(int *)(a + 2) = 5;
    expect(5, a[2]);
}
