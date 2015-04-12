// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <stdarg.h>
#include "test.h"

static void test_builtin() {
#ifdef __8cc__
    expect(0, __builtin_reg_class((int *)0));
    expect(1, __builtin_reg_class((float *)0));
    expect(2, __builtin_reg_class((struct{ int x; }*)0));
#endif
}

static void test_int(int a, ...) {
    va_list ap;
    va_start(ap, a);
    expect(1, a);
    expect(2, va_arg(ap, int));
    expect(3, va_arg(ap, int));
    expect(5, va_arg(ap, int));
    expect(8, va_arg(ap, int));
    va_end(ap);
}

static void test_float(float a, ...) {
    va_list ap;
    va_start(ap, a);
    expectf(1.0, a);
    expectd(2.0, va_arg(ap, double));
    expectd(4.0, va_arg(ap, double));
    expectd(8.0, va_arg(ap, double));
    va_end(ap);
}

static void test_mix(char *p, ...) {
    va_list ap;
    va_start(ap, p);
    expect_string("abc", p);
    expectd(2.0, va_arg(ap, double));
    expect(4, va_arg(ap, int));
    expect_string("d", va_arg(ap, char *));
    expect(5, va_arg(ap, int));
    va_end(ap);
}

char *fmt(char *fmt, ...) {
    static char buf[100];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    return buf;
}

static void test_va_list() {
    expect_string("", fmt(""));
    expect_string("3", fmt("%d", 3));
    expect_string("3,1.0,6,2.0,abc", fmt("%d,%.1f,%d,%.1f,%s", 3, 1.0, 6, 2.0, "abc"));
}

void testmain() {
    print("varargs");
    test_builtin();
    test_int(1, 2, 3, 5, 8);
    test_float(1.0, 2.0, 4.0, 8.0);
    test_mix("abc", 2.0, 4, "d", 5);
    test_va_list();
}
