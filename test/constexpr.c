// Copyright 2015 Rui Ueyama. Released under the MIT license.

#include "test.h"

int x1[] = { 1, 2, 3, 4, 5 };
int *p1 = x1;
int *q1 = x1 + 2;

int x2 = 7;
int *p2 = &x2 + 1;

void testmain() {
    print("constexpr");
    expect(1, *p1);
    expect(3, *q1);
    expect(7, p2[-1]);
}
