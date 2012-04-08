#include "test.h"

void testmain(void) {
    print("compound assignment");

    int a = 0;
    a += 5;
    expect(5, a);
    a -= 2;
    expect(3, a);
    a *= 10;
    expect(30, a);
    a /= 2;
    expect(15, a);
    a %= 6;
    expect(3, a);

    a = 14;
    a &= 7;
    expect(6, a);
    a |= 8;
    expect(14, a);
    a ^= 3;
    expect(13, a);
    a <<= 2;
    expect(52, a);
    a >>= 2;
    expect(13, a);
}
