// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

void testmain(void) {
    print("numeric constants");

    expect(1, 0x1);
    expect(17, 0x11);
    expect(511, 0777);
    expect(11, 0b1011);  // GNU extension
    expect(11, 0B1011);  // GNU extension

    expect(3, 3L);
    expect(3, 3LL);
    expect(3, 3UL);
    expect(3, 3LU);
    expect(3, 3ULL);
    expect(3, 3LU);
    expect(3, 3LLU);

    expectd(55.3, 55.3);
    expectd(200, 2e2);
    expectd(0x0.DE488631p8, 0xDE.488631);

    expect(4, sizeof(5));
    expect(8, sizeof(5L));
    expect(4, sizeof(3.0f));
    expect(8, sizeof(3.0));
}
