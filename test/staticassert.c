// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

void testmain() {
    print("static assert");
    _Static_assert(1, "fail");

    struct {
        _Static_assert(1, "fail");
    } x;
}
