// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

extern int expect(int, int);
extern int externvar1;
int extern externvar2;

void testmain(void) {
    print("extern");
    expect(98, externvar1);
    expect(99, externvar2);
}
