// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

void testmain() {
    print("statement expression");

    expect(3, ({ 1; 2; 3; }));
    expectf(3.0, ({ 1; 2; 3.0; }));
    expect(5, ({ int a = 5; a; }));
}
