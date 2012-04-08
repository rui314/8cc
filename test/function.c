#include "test.h"

int t1(void) {
    return 77;
}

void t2(int a) {
    expect(79, a);
}

void t3(int a, int b, int c, int d, int e, int f) {
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

void t4(void) {
    int a[] = { 98 };
    expect(98, t4a(a));
}

void t5a(int *p) {
    expect(99, *p); p=p+1;
    expect(98, *p); p=p+1;
    expect(97, *p);
}

void t5b(int p[]) {
    expect(99, *p); p=p+1;
    expect(98, *p); p=p+1;
    expect(97, *p);
}

void t5(void) {
    int a[] = {1, 2, 3};
    int *p = a;
    *p = 99; p = p + 1;
    *p = 98; p = p + 1;
    *p = 97;
    t5a(a);
    t5b(a);
}

int t6();
int t6(void) {
    return 3;
}

int t7(int a, int b);
int t7(int a, int b) {
    return a * b;
}

int t8(int a, ...) {
    expect(23, a);
}

void t9(void) {
    return;
}

int ptrtest1(void) {
    return 55;
}

int ptrtest2(int a) {
    return a * 2;
}

float ptrtest3(float a) {
    return a * 2;
}

void func_ptr_call(void) {
    int (*p1)(void) = ptrtest1;
    expect(55, p1());
    int (*p2)(int) = ptrtest2;
    expect(110, p2(55));
    float (*p3)(float) = ptrtest3;
    expectf(4, p3(2));
}

void func_name(void) {
    expect_string("func_name", __func__);
}

void empty(void) {
}

void empty2(void) {
    ;;;
}

void testmain(void) {
    print("function");

    expect(77, t1());
    t2(79);
    t3(1, 2, 3, 4, 5, 6);
    t4();
    t5();
    expect(3, t6());
    expect(12, t7(3, 4));
    t8(23);
    t9();
    func_ptr_call();
    func_name();
    empty();
    empty2();
}
