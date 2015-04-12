// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

#ifdef __8cc__
#pragma disable_warning
#endif

// Defined in main/testmain.c
int oldstyle1();

oldstyle2() {
    return 4;
}

oldstyle3(a) {
    return a;
}

oldstyle4(a, b)
double b;
{
    return a + b;
}

void testmain() {
    print("K&R");
    expect(3, no_declaration());
    expect(7, oldstyle1(3, 4));
    expect(4, oldstyle2());
    expect(5, oldstyle3(5));
    expect(9, oldstyle4(5, 4.0));
}

int no_declaration() {
    return 3;
}
