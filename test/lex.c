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

#ifdef __8cc__
#pragma disable_warning
#endif

    expect(10, val\    
ue);

#ifdef __8cc__
#pragma enable_warning
#endif
}

void whitespace(void) {
    expect_string("x y", stringify(xy));
}

void newline(void) {
     
#
}

void dollar(void) {
    int $ = 1;
    expect(1, $);
    int $2 = 2;
    expect(2, $2);
    int a$ = 3;
    expect(3, a$);
}

void testmain(void) {
    print("lexer");
    digraph();
    escape();
    whitespace();
    newline();
    dollar();
}
