// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

#define stringify(x) %:x
#define paste(x, y) x%:%:y

static void digraph(void) {
    // These tests don't conform to the C standard.
    // N1570 6.4.6.3 says that the digraphs behave the same
    // as the corresponding tokens except for their spellings.
    // That implies the compiler should preserve the original
    // spelling instead of replacing digraphs with regular tokens.
    // I intentionally leave this bug because that's really a minor
    // bug which doesn't worth the complexity to be handled correctly.
#ifdef __8cc__
    expect_string("[", stringify(<:));
    expect_string("]", stringify(:>));
    expect_string("{", stringify(<%));
    expect_string("}", stringify(%>));
    expect_string("#", stringify(%:));
    expect_string("% :", stringify(% :));
    expect_string("##", stringify(%:%:));
    expect_string("#%", stringify(%:%));
    expect(12, paste(1, 2));
#endif
}

static void escape(void) {
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

static void whitespace(void) {
    expect_string("x y", stringify(xy));
}

static void newline(void) {
     
#
}

static void dollar(void) {
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
