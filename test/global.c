// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

defaultint;

int val = 21;
int *p1 = &val;

int a1[3];
int a2[3] = { 24, 25, 26 };
int x1, x2;
int x3, x4 = 4;
int x5 = 5, x6;

char s1[] = "abcd";
char *s2 = "ABCD";
long l1 = 8;
int *intp = &(int){ 9 };

void testmain() {
    print("global variable");

    defaultint = 3;
    expect(3, defaultint);

    expect(21, val);
    val = 22;
    expect(22, val);
    expect(22, *p1);

    a1[1] = 23;
    expect(23, a1[1]);
    expect(25, a2[1]);

    x1 = 1;
    x2 = 2;
    expect(1, x1);
    expect(2, x2);
    x3 = 3;
    expect(3, x3);
    expect(4, x4);
    expect(5, x5);
    x6 = 6;
    expect(6, x6);

    expect_string("abcd", s1);
    expect_string("ABCD", s2);

    expectl(8, l1);
    expectl(9, *intp);
}
