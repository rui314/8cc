// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

#ifdef __8cc__
#pragma disable_warning
#endif

void testmain(void) {
    print("K&R");
    expect(3, no_declaration());
}

int no_declaration() {
    return 3;
}
