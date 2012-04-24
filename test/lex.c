// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

#define stringify(x) #x

void digraph(void) {
    expect_string("[", stringify(<:));
    expect_string("]", stringify(:>));
    expect_string("{", stringify(<%));
    expect_string("}", stringify(%>));
    expect_string("#", stringify(%:));
    expect_string("% :", stringify(% :));
    expect_string("##", stringify(%:%:));
    expect_string("#%", stringify(%:%));
}

void escape(void) {
    int value = 10;
    expect(10, val\
ue);
}

void testmain(void) {
    print("lexer");
    digraph();
    escape();
}
