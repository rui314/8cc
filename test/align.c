// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"
#include <stdalign.h>

void test_alignof(void) {
    expect(1, __alignof_is_defined);
    expect(1, alignof(char));
    expect(4, alignof(int));
    expect(8, alignof(struct {char a; int b; }));
}

void testmain(void) {
    print("alignment");
    test_alignof();
}
