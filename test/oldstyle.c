// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

#ifdef __8cc__
#pragma disable_warning
#endif

oldstyle1() {
    return 4;
}

oldstyle2(a) {
    return a;
}

oldstyle3(a, b)
double b;
{
    return a + b;
}

void testmain(void) {
    print("K&R");
    expect(3, no_declaration());
    expect(4, oldstyle1());
    expect(5, oldstyle2(5));
    expect(9, oldstyle3(5, 4.0));
}

int no_declaration() {
    return 3;
}
