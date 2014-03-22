// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

static void t1(void) {
    union { int a; int b; } x;
    x.a = 90;
    expect(90, x.b);
}

static void t2(void) {
    union { char a[4]; int b; } x;
    x.b = 0;
    x.a[1] = 1;
    expect(256, x.b);
}

static void t3(void) {
    union { char a[4]; int b; } x;
    x.a[0] = x.a[1] = x.a[2] = x.a[3] = 0;
    x.a[1]=1;
    expect(256, x.b);
}

void testmain(void) {
    print("union");
    t1();
    t2();
    t3();
}
