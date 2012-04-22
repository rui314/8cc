// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "test.h"

float tf1(float a)  { return a; }
float tf2(double a) { return a; }
float tf3(int a)    { return a; }

double td1(float a)  { return a; }
double td2(double a) { return a; }
double td3(int a)    { return a; }

double recursive(double a) {
    if (a < 10) return a;
    return recursive(3.33);
}

void testmain(void) {
    print("float");

    expect(0.7, .7);
    float v1 = 10.0;
    float v2 = v1;
    expectf(10.0, v1);
    expectf(10.0, v2);
    return;
    double v3 = 20.0;
    double v4 = v3;
    expectd(20.0, v3);
    expectd(20.0, v4);

    expectf(1.0, 1.0);
    expectf(1.5, 1.0 + 0.5);
    expectf(0.5, 1.0 - 0.5);
    expectf(2.0, 1.0 * 2.0);
    expectf(0.25, 1.0 / 4.0);

    expectf(3.0, 1.0 + 2);
    expectf(2.5, 5 - 2.5);
    expectf(2.0, 1.0 * 2);
    expectf(0.25, 1.0 / 4);

    expectf(10.5, tf1(10.5));
    expectf(10.0, tf1(10));
    expectf(10.6, tf2(10.6));
    expectf(10.0, tf2(10));
    expectf(10.0, tf3(10.7));
    expectf(10.0, tf3(10));

    expectd(1.0, tf1(1.0));
    expectd(10.0, tf1(10));
    expectd(2.0, tf2(2.0));
    expectd(10.0, tf2(10));
    expectd(11.0, tf3(11.5));
    expectd(10.0, tf3(10));

    expectd(3.33, recursive(100));
}
