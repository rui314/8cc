// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

int a;

int *p1 = &a;
int *p2 = &a + 1;
int *p3 = 1 + &a;


void testmain(void) {
    print("constexpr");
    
    expectp(p1, &a);
    expectp(p2, &a + 1);
    expectp(p3, &a + 1);
}

