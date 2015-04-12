// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void expects(short a, short b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

void testmain() {
    print("long");

    short a = 10;
    short int b = 15;
    expects(25, a + b);
    expects(20, a + 10);

    long x = 67;
    long int y = 69;
    expectl(67, x);
    expectl(136, x + y);
    expectl(10L, 10L);
    expectl(4294967295L, 4294967295);
    expectl(1152921504606846976, 1152921504606846976);
    expectl(1152921504606846977, 1152921504606846976 + 1);
}
