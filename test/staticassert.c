// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

void testmain(void) {
    print("static assert");
    _Static_assert(1, "fail");

    struct {
        _Static_assert(1, "fail");
    } x;
}
