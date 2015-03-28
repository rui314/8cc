// Copyright 2014 Rui Ueyama. Released under the MIT license.

#include "test.h"
#include <stdnoreturn.h>

// _Noreturn is ignored
_Noreturn void f1();
noreturn void f2();
inline void f3() {}

void testmain() {
    print("noreturn");
}
