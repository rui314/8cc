// Copyright 2012 Rui Ueyama. Released under the MIT license.

// This test depends on the stack frame and code layout and does not
// run with gcc -O2.

#include "test.h"

#ifdef __8cc__

static void *test_return_address_sub2() {
    return __builtin_return_address(1);
}

static void *test_return_address_sub1() {
    expect((long)__builtin_return_address(0), (long)test_return_address_sub2());
    return __builtin_return_address(0);
}

static void test_return_address() {
    void *ptr;
 L1:
    ptr = test_return_address_sub1();
 L2:
    expect(1, &&L1 < ptr && ptr <= &&L2);
}

#else
static void test_return_address() {}
#endif

void testmain() {
    print("builtin");
    test_return_address();
}
