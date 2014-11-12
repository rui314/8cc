// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

// import.h would raise an error if read twice.
#import "import.h"
#import "import.h"

// once.h would raise an error if read twice
#include "once.h"
#include "once.h"

void testmain(void) {
    print("import");
}
