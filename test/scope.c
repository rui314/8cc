// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

void testmain() {
    print("scope");

    int a = 31;
    { int a = 64; }
    expect(31, a);
    {
        int a = 64;
        expect(64, a);
    }
}
