// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void t1() {
    int a[2][3];
    int *p = a;
    *p = 1;
    expect(1, *p);
}

static void t2() {
    int a[2][3];
    int *p = a + 1;
    *p = 1;
    int *q = a;
    *p = 32;
    expect(32, *(q + 3));
}

static void t3() {
    int a[4][5];
    int *p = a;
    *(*(a + 1) + 2) = 62;
    expect(62, *(p + 7));
}

static void t4() {
    int a[3] = { 1, 2, 3 };
    expect(1, a[0]);
    expect(2, a[1]);
    expect(3, a[2]);
}

static void t5() {
    int a[2][3];
    a[0][1] = 1;
    a[1][1] = 2;
    int *p = a;
    expect(1, p[1]);
    expect(2, p[4]);
}

static void t6a(int e, int x[][3]) {
    expect(e, *(*(x + 1) + 1));
}

static void t6() {
    int a[2][3];
    int *p = a;
    *(p + 4) = 65;
    t6a(65, a);
}

static void t7() {
    int a[3*3];  // integer constant expression
    a[8] = 68;
    expect(68, a[8]);
}

void testmain() {
    print("array");
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
    t7();
}
