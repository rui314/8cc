// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"
#include <stdbool.h>

int t1() {
    return 77;
}

static void t2(int a) {
    expect(79, a);
}

static void t3(int a, int b, int c, int d, int e, int f) {
    expect(1, a);
    expect(2, b);
    expect(3, c);
    expect(4, d);
    expect(5, e);
    expect(6, f);
}

int t4a(int *p) {
    return *p;
}

static void t4() {
    int a[] = { 98 };
    expect(98, t4a(a));
}

static void t5a(int *p) {
    expect(99, *p); p=p+1;
    expect(98, *p); p=p+1;
    expect(97, *p);
}

static void t5b(int p[]) {
    expect(99, *p); p=p+1;
    expect(98, *p); p=p+1;
    expect(97, *p);
}

static void t5() {
    int a[] = {1, 2, 3};
    int *p = a;
    *p = 99; p = p + 1;
    *p = 98; p = p + 1;
    *p = 97;
    t5a(a);
    t5b(a);
}

int t6();
int t6() {
    return 3;
}

int t7(int a, int b);
int t7(int a, int b) {
    return a * b;
}

int t8(int a, ...) {
    expect(23, a);
}

static void t9() {
    return;
}

int t10(int a, double b) {
    return a + b;
}

int ptrtest1() {
    return 55;
}

int ptrtest2(int a) {
    return a * 2;
}

float ptrtest3(float a) {
    return a * 2;
}

int ptrtest4(int (f)(int), int x) {
    return f(x);
}

static void func_ptr_call() {
    expectf(4, ptrtest3(2));
    int (*p1)(void) = ptrtest1;
    expect(55, p1());
    int (*p2)(int) = ptrtest2;
    expect(110, p2(55));
    float (*p3)(float) = ptrtest3;
    expectf(4, p3(2));
    int (*p4)(void) = &ptrtest1;
    expect(55, (**p4)());
    expect(10, ptrtest4(ptrtest2, 5));
}

static void func_name() {
    expect_string("func_name", __func__);
    expect_string("func_name", __FUNCTION__);
}

static int local_static2() {
    static int x = 1;
    static char y[] = "2";
    static int z;
    z = 3;
    return x++ + (y[0] - '0') + z;
}

static void local_static3() {
    static int x = 5;
    static char y[] = "8";
    static int z;
    z = 100;
}

static void local_static() {
    expect(6, local_static2());
    expect(7, local_static2());
    local_static3();
    expect(8, local_static2());
}

static void empty() {
}

static void empty2() {
    ;;;
}

int booltest1(int x);

bool booltest2(int x) {
    return x;
}

static void test_bool() {
    expect(0, booltest1(256));
    expect(1, booltest1(257));
    expect(1, booltest2(512));
    expect(1, booltest2(513));
}

typedef struct { int a, b, c, d; } MyType;

int sum(MyType x) {
    return x.a + x.b + x.c + x.d;
}

static void test_struct() {
    expect(14, sum((MyType){ 2, 3, 4, 5 }));
}

static void test_funcdesg() {
    test_funcdesg;
}

typedef int (*t6_t)(void);

static t6_t retfunc() {
  return &t6;
}

static t6_t retfunc2() {
  return t6;
}

// _Alignas is a declaration specifier containing parentheses.
// Make sure the compiler doesn't interpret it as a function definition.
static _Alignas(32) char char32;

void testmain() {
    print("function");

    expect(77, t1());
    t2(79);
    t3(1, 2, 3, 4, 5, 6);
    t4();
    t5();
    expect(3, t6());
    expect(12, t7(3, 4));
    expect(77, (1 ? t1 : t6)());
    expect(3, (0 ? t1 : t6)());
    t8(23);
    t9();
    expect(7, t10(3, 4.0));
    func_ptr_call();
    func_name();
    local_static();
    empty();
    empty2();
    test_bool();
    test_struct();
    test_funcdesg();
    expect(3, retfunc()());
    expect(3, retfunc2()());
}
