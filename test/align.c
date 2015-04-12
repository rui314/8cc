// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"
#include <stdalign.h>
#include <stddef.h>

static void test_alignas() {
    expect(1, offsetof(struct { char x; char y; }, y));
    expect(4, offsetof(struct { char x; _Alignas(4) char y; }, y));
    expect(4, offsetof(struct { char x; _Alignas(int) char y; }, y));
    expect(1, offsetof(struct { char x; alignas(0) char y; }, y));
}

static void test_alignof() {
    expect(1, __alignof_is_defined);
    expect(1, _Alignof(char));
    expect(1, __alignof__(char));
    expect(1, alignof(char));
    expect(2, alignof(short));
    expect(4, alignof(int));
    expect(8, alignof(double));
    expect(1, alignof(char[10]));
    expect(8, alignof(double[10]));
    expect(1, _Alignof(struct {}));
    expect(4, alignof(struct {char a; int b; }));
#ifdef __8cc__
    expect(8, alignof(struct {int a; long double b; }));
    expect(8, alignof(long double));
#endif

    // The type of the result is size_t.
    expect(1, alignof(char) - 2 > 0);
}

static void test_constexpr() {
    char a[alignof(int)];
    expect(4, sizeof(a));
}

void testmain() {
    print("alignment");
    test_alignas();
    test_alignof();
    test_constexpr();
}
