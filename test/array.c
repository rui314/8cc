// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

void t1(void) {
    int a[2][3];
    int *p = a;
    *p = 1;
    expect(1, *p);
}

void t2(void) {
    int a[2][3];
    int *p = a + 1;
    *p = 1;
    int *q = a;
    *p = 32;
    expect(32, *(q + 3));
}

void t3(void) {
    int a[4][5];
    int *p = a;
    *(*(a + 1) + 2) = 62;
    expect(62, *(p + 7));
}

void t4(void) {
    int a[3] = { 1, 2, 3 };
    expect(1, a[0]);
    expect(2, a[1]);
    expect(3, a[2]);
}

void t5(void) {
    int a[2][3];
    a[0][1] = 1;
    a[1][1] = 2;
    int *p = a;
    expect(1, p[1]);
    expect(2, p[4]);
}

void t6a(int e, int x[][3]) {
    expect(e, *(*(x + 1) + 1));
}

void t6(void) {
    int a[2][3];
    int *p = a;
    *(p + 4) = 65;
    t6a(65, a);
}

void t7(void) {
    int a[3*3];  // integer constant expression
    a[8] = 68;
    expect(68, a[8]);
}

void testmain(void) {
    print("array");
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
}
