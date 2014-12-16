// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <string.h>
#include "test.h"

void testmain(void) {
    print("#line");

#line 99
    expect(99, __LINE__);

#line 199 "foo"
    expect(199, __LINE__);
    expect_string("foo", __FILE__);

#define X 3
#line X
    expect(3, __LINE__);

#define Y 5 "bar"
#line Y
    expect(5, __LINE__);
    expect_string("bar", __FILE__);
}
