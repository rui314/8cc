// Copyright 2014 Rui Ueyama. Released under the MIT license.

#include "test.h"

#if __8cc__

#include "includeguard1.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif
#include "includeguard1.h"
#if __8cc_include_guard == 0
# error "include guard"
#endif

#include "includeguard2.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif
#include "includeguard2.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif

#include "includeguard3.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif
#include "includeguard3.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif

#include "includeguard4.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif
#include "includeguard4.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif

#include "includeguard5.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif
#include "includeguard5.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif

#include "includeguard6.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif
#include "includeguard6.h"
#if __8cc_include_guard == 1
# error "include guard"
#endif

#endif

void testmain() {
    print("include guard");
}
