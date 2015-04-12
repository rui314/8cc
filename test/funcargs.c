// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"

static void many_ints(int v1, int v2, int v3, int v4, int v5, int v6, int v7, int v8, int v9) {
    expect(1, v1); expect(2, v2); expect(3, v3); expect(4, v4);
    expect(5, v5); expect(6, v6); expect(7, v7); expect(8, v8);
    expect(9, v9);
}

static void many_floats(float v01, float v02, float v03, float v04, float v05,
                 float v06, float v07, float v08, float v09, float v10,
                 float v11, float v12, float v13, float v14, float v15,
                 float v16, float v17) {
    expectf(1, v01);  expectf(2, v02);  expectf(3, v03);  expectf(4, v04);
    expectf(5, v05);  expectf(6, v06);  expectf(7, v07);  expectf(8, v08);
    expectf(9, v09);  expectf(10, v10); expectf(11, v11); expectf(12, v12);
    expectf(13, v13); expectf(14, v14); expectf(15, v15); expectf(16, v16);
    expectf(17, v17);
}

static void mixed(float v01, int v02, float v03, int v04, float v05, int v06, float v07, int v08,
           float v09, int v10, float v11, int v12, float v13, int v14, float v15, int v16,
           float v17, int v18, float v19, int v20, float v21, int v22, float v23, int v24,
           float v25, int v26, float v27, int v28, float v29, int v30, float v31, int v32,
           float v33, int v34, float v35, int v36, float v37, int v38, float v39, int v40) {
    expectf(1.0, v01);  expect(2, v02);  expectf(3.0, v03);  expect(4, v04);
    expectf(5.0, v05);  expect(6, v06);  expectf(7.0, v07);  expect(8, v08);
    expectf(9.0, v09);  expect(10, v10); expectf(11.0, v11); expect(12, v12);
    expectf(13.0, v13); expect(14, v14); expectf(15.0, v15); expect(16, v16);
    expectf(17.0, v17); expect(18, v18); expectf(19.0, v19); expect(20, v20);
    expectf(21.0, v21); expect(22, v22); expectf(23.0, v23); expect(24, v24);
    expectf(25.0, v25); expect(26, v26); expectf(27.0, v27); expect(28, v28);
    expectf(29.0, v29); expect(30, v30); expectf(31.0, v31); expect(32, v32);
    expectf(33.0, v33); expect(34, v34); expectf(35.0, v35); expect(36, v36);
    expectf(37.0, v37); expect(38, v38); expectf(39.0, v39); expect(40, v40);
}

void testmain() {
    print("function argument");

    many_ints(1, 2, 3, 4, 5, 6, 7, 8, 9);

    many_floats(1.0, 2.0,  3.0,  4.0,  5.0,  6.0,  7.0,  8.0,
                9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0,
                17.0);

    mixed(1.0,  2,  3.0,  4,  5.0,  6,  7.0,  8,  9.0,  10,
          11.0, 12, 13.0, 14, 15.0, 16, 17.0, 18, 19.0, 20,
          21.0, 22, 23.0, 24, 25.0, 26, 27.0, 28, 29.0, 30,
          31.0, 32, 33.0, 34, 35.0, 36, 37.0, 38, 39.0, 40);
}
