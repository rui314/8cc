// Copyright 2014 Rui Ueyama. Released under the MIT license.

#include <string.h>
#include "test.h"

void testmain() {
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

# 1 "xyz"
    expect(1, __LINE__);
    expect_string("xyz", __FILE__);

# 2 "XYZ" 1 3 4
    expect(2, __LINE__);
    expect_string("XYZ", __FILE__);
}
