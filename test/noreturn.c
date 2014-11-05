// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"
#include <stdnoreturn.h>

// _Noreturn is ignored
_Noreturn void f1();
noreturn void f2();
inline void f3() {}

void testmain(void) {
    print("noreturn");
}
