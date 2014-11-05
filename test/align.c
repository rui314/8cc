// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"
#include <stdalign.h>

static void test_alignof(void) {
    expect(1, __alignof_is_defined);
    expect(1, _Alignof(char));
    expect(1, __alignof__(char));
    expect(1, alignof(char));
    expect(2, alignof(short));
    expect(4, alignof(int));
    expect(8, alignof(double));

    expect(1, alignof(char[10]));
    expect(8, alignof(double[10]));
    expect(16, alignof(struct {int a; long double b; }));
#ifdef __8cc__
    expect(0, _Alignof(struct {}));
    expect(8, alignof(struct {char a; int b; }));
    expect(8, alignof(long double));
#endif
}

void testmain(void) {
    print("alignment");
    test_alignof();
}
