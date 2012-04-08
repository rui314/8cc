#include "test/test.h"

void t1(void) {
    int a = 1;
    expect(3, a + 2);
}

void t2(void) {
    int a = 1;
    int b = 48 + 2;
    int c = a + b;
    expect(102, c * 2);
}

void t3(void) {
    int a[] = { 55 };
    int *b = a;
    expect(55, *b);
}

void t4(void) {
    int a[] = { 55, 67 };
    int *b = a + 1;
    expect(67, *b);
}

void t5(void) {
    int a[] = { 20, 30, 40 };
    int *b = a + 1;
    expect(30, *b);
}

void t6(void) {
    int a[] = { 20, 30, 40 };
    expect(20, *a);
}

void testmain(void) {
    print("declaration");
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
}
