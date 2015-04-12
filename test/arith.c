// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void test_basic() {
    expect(0, 0);
    expect(3, 1 + 2);
    expect(3, 1 + 2);
    expect(10, 1 + 2 + 3 + 4);
    expect(11, 1 + 2 * 3 + 4);
    expect(14, 1 * 2 + 3 * 4);
    expect(4, 4 / 2 + 6 / 3);
    expect(4, 24 / 2 / 3);
    expect(3, 24 % 7);
    expect(0, 24 % 3);
    expect(98, 'a' + 1);
    int a = 0 - 1;
    expect(0 - 1, a);
    expect(-1, a);
    expect(0, a + 1);
    expect(1, +1);
    expect(1, (unsigned)4000000001 % 2);
}

static void test_relative() {
    expect(1, 1 > 0);
    expect(1, 0 < 1);
    expect(0, 1 < 0);
    expect(0, 0 > 1);
    expect(0, 1 > 1);
    expect(0, 1 < 1);
    expect(1, 1 >= 0);
    expect(1, 0 <= 1);
    expect(0, 1 <= 0);
    expect(0, 0 >= 1);
    expect(1, 1 >= 1);
    expect(1, 1 <= 1);
    expect(1, 0xFFFFFFFFU > 1);
    expect(1, 1 < 0xFFFFFFFFU);
    expect(1, 0xFFFFFFFFU >= 1);
    expect(1, 1 <= 0xFFFFFFFFU);
    expect(1, -1 > 1U);
    expect(1, -1 >= 1U);
    expect(0, -1L > 1U);
    expect(0, -1L >= 1U);
    expect(0, 1.0 < 0.0);
    expect(1, 0.0 < 1.0);
}

static void test_inc_dec() {
    int a = 15;
    expect(15, a++);
    expect(16, a);
    expect(16, a--);
    expect(15, a);
    expect(14, --a);
    expect(14, a);
    expect(15, ++a);
    expect(15, a);
}

static void test_bool() {
    expect(0, !1);
    expect(1 ,!0);
}

static void test_ternary() {
    expect(51, (1 + 2) ? 51 : 52);
    expect(52, (1 - 1) ? 51 : 52);
    expect(26, (1 - 1) ? 51 : 52 / 2);
    expect(17, (1 - 0) ? 51 / 3 : 52);
    // GNU extension
    expect(52, 0 ?: 52);
    expect(3, (1 + 2) ?: 52);
}

static void test_unary() {
    char x = 2;
    short y = 2;
    int z = 2;
    expect(-2, -x);
    expect(-2, -y);
    expect(-2, -z);
}

static void test_comma() {
    expect(3, (1, 3));
    expectf(7.0, (1, 3, 5, 7.0));
}

void testmain() {
    print("basic arithmetic");
    test_basic();
    test_relative();
    test_inc_dec();
    test_bool();
    test_unary();
    test_ternary();
    test_comma();
}
