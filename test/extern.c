// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

extern int externvar1;
int extern externvar2;

void testmain() {
    print("extern");
    expect(98, externvar1);
    expect(99, externvar2);
}
