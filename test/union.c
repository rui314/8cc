#include "test/test.h"

void t1(void) {
    union { int a; int b; } x;
    x.a = 90;
    expect(90, x.b);
}

void t2(void) {
    union { char a[4]; int b; } x;
    x.b = 0;
    x.a[1] = 1;
    expect(256, x.b);
}

void t3(void) {
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
