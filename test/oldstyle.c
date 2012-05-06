// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

#ifdef __8cc__
#pragma disable_warning
#endif

oldstyle() {
    return 4;
}

void testmain(void) {
    print("K&R");
    expect(3, no_declaration());
    expect(4, oldstyle());
}

int no_declaration() {
    return 3;
}
