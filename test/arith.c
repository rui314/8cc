// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

static void test_basic(void) {
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
}

static void test_relative(void) {
    expect(1, 1 > 0);
    expect(1, 0 < 1);
}

static void test_inc_dec(void) {
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

static void test_bool(void) {
    expect(0, !1);
    expect(1 ,!0);
}

static void test_ternary(void) {
    expect(51, (1 + 2) ? 51 : 52);
    expect(52, (1 - 1) ? 51 : 52);
    expect(26, (1 - 1) ? 51 : 52 / 2);
    expect(17, (1 - 0) ? 51 / 3 : 52);
    expect(3, (1 + 2) ?: 52);
}

static void test_comma(void) {
    expect(3, (1, 3));
    expectf(7.0, (1, 3, 5, 7.0));
}

void testmain(void) {
    print("basic arithmetic");
    test_basic();
    test_relative();
    test_inc_dec();
    test_bool();
    test_ternary();
    test_comma();
}
