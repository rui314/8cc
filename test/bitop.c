#include "test/test.h"

void test_or() {
    expect(3, 1 | 2);
    expect(7, 2 | 5);
    expect(7, 2 | 7);
}

void test_and() {
    expect(0, 1 & 2);
    expect(2, 2 & 7);
}

void test_not() {
    expect(-1, ~0);
    expect(-3, ~2);
    expect(0, ~-1);
}

void test_xor() {
    expect(10, 15 ^ 5);
}

void test_shift() {
    expect(16, 1 << 4);
    expect(48, 3 << 4);

    expect(1, 15 >> 3);
    expect(2, 8 >> 2);
}

void testmain() {
    print("bitwise operators");
    test_or();
    test_and();
    test_not();
    test_xor();
    test_shift();
}
