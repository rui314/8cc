// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void t1() {
    int a = 1;
    expect(3, a + 2);
}

static void t2() {
    int a = 1;
    int b = 48 + 2;
    int c = a + b;
    expect(102, c * 2);
}

static void t3() {
    int a[] = { 55 };
    int *b = a;
    expect(55, *b);
}

static void t4() {
    int a[] = { 55, 67 };
    int *b = a + 1;
    expect(67, *b);
}

static void t5() {
    int a[] = { 20, 30, 40 };
    int *b = a + 1;
    expect(30, *b);
}

static void t6() {
    int a[] = { 20, 30, 40 };
    expect(20, *a);
}

static int ((t7))();
static int ((*t8))();
static int ((*(**t9))(int*(), int(*), int()));

void testmain() {
    print("declaration");
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
}
