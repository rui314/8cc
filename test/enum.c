// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

enum { g1, g2, g3 } global1;

void testmain() {
    print("enum");

    expect(0, g1);
    expect(2, g3);

    enum { x } v;
    expect(0, x);

    enum { y };
    expect(0, y);

    enum tag { z };
    enum tag a = z;
    expect(0, z);
    expect(0, a);
}
