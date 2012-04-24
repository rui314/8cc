// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"
#include <stdbool.h>

void testmain(void) {
    print("sizeof");

    expect(1, sizeof(void));
    expect(1, sizeof(testmain));
    expect(1, sizeof(char));
    expect(1, sizeof(_Bool));
    expect(1, sizeof(bool));
    expect(2, sizeof(short));
    expect(4, sizeof(int));
    expect(8, sizeof(long));

    expect(8, sizeof(char *));
    expect(8, sizeof(short *));
    expect(8, sizeof(int *));
    expect(8, sizeof(long *));

    expect(1, sizeof(unsigned char));
    expect(2, sizeof(unsigned short));
    expect(4, sizeof(unsigned int));
    expect(8, sizeof(unsigned long));

    expect(4, sizeof 1);
    expect(8, sizeof 1L);
    expect(8, sizeof 1.0);

    expect(1, sizeof(char[1]));
    expect(7, sizeof(char[7]));
    expect(30, sizeof(char[3][10]));
    expect(32, sizeof(int[4][2]));

    expect(4, sizeof('a'));
    expect(4, sizeof(1));
    expect(8, sizeof(1L));
    expect(4, sizeof(1.0f));
    expect(8, sizeof(1.0));

    char a[] = { 1, 2, 3 };
    expect(3, sizeof(a));
    char b[] = "abc";
    expect(4, sizeof(b));
    char *c[5];
    expect(40, sizeof(c));
    char *(*d)[3];
    expect(8, sizeof(d));
    expect(24, sizeof(*d));
    expect(8, sizeof(**d));
    expect(1, sizeof(***d));
}
