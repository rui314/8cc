// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

void testmain() {
    print("statement expression");

    expect(3, ({ 1; 2; 3; }));
    expectf(3.0, ({ 1; 2; 3.0; }));
    expect(5, ({ int a = 5; a; }));
}
