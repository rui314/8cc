// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <float.h>
#include <stdarg.h>
#include <stdint.h>
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

char *fmt(char *fmt, ...) {
    static char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

char *fmtint(int x) { return fmt("%d", x); }
char *fmtdbl(double x) { return fmt("%a", x); }

void std() {
    expect_string("21", fmtint(DECIMAL_DIG));
    expect_string("0", fmtint(FLT_EVAL_METHOD));
    expect_string("2", fmtint(FLT_RADIX));
    expect_string("1", fmtint(FLT_ROUNDS));

    expect_string("6", fmtint(FLT_DIG));
    expect_string("0x1p-23", fmtdbl(FLT_EPSILON));
    expect_string("24", fmtint(FLT_MANT_DIG));
    expect_string("0x1.fffffep+127", fmtdbl(FLT_MAX));
    expect_string("38", fmtint(FLT_MAX_10_EXP));
    expect_string("128", fmtint(FLT_MAX_EXP));
    expect_string("0x1p-126", fmtdbl(FLT_MIN));
    expect_string("-37", fmtint(FLT_MIN_10_EXP));
    expect_string("-125", fmtint(FLT_MIN_EXP));
    expectd(*(float *)&(uint32_t){1}, FLT_TRUE_MIN);
    expect_string("0x1p-149", fmtdbl(FLT_TRUE_MIN));

    expect_string("15", fmtint(DBL_DIG));
    expect_string("0x1p-52", fmtdbl(DBL_EPSILON));
    expect_string("53", fmtint(DBL_MANT_DIG));
    expect_string("0x1.fffffffffffffp+1023", fmtdbl(DBL_MAX));
    expect_string("308", fmtint(DBL_MAX_10_EXP));
    expect_string("1024", fmtint(DBL_MAX_EXP));
    expect_string("0x1p-1022", fmtdbl(DBL_MIN));
    expect_string("-307", fmtint(DBL_MIN_10_EXP));
    expect_string("-1021", fmtint(DBL_MIN_EXP));
    expectd(*(double *)&(uint64_t){1}, DBL_TRUE_MIN);
    expect_string("0x0.0000000000001p-1022", fmtdbl(DBL_TRUE_MIN));

#ifdef __8cc__
    expect_string("15", fmtint(LDBL_DIG));
    expect_string("0x1p-52", fmtdbl(LDBL_EPSILON));
    expect_string("53", fmtint(LDBL_MANT_DIG));
    expect_string("0x1.fffffffffffffp+1023", fmtdbl(LDBL_MAX));
    expect_string("308", fmtint(LDBL_MAX_10_EXP));
    expect_string("1024", fmtint(LDBL_MAX_EXP));
    expect_string("0x1p-1022", fmtdbl(LDBL_MIN));
    expect_string("-307", fmtint(LDBL_MIN_10_EXP));
    expect_string("-1021", fmtint(LDBL_MIN_EXP));
    expectd(*(double *)&(uint64_t){1}, LDBL_TRUE_MIN);
    expect_string("0x0.0000000000001p-1022", fmtdbl(LDBL_TRUE_MIN));
#endif
}

void testmain() {
    print("float");
    std();

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
