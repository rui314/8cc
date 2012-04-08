#include "test.h"
#include <stdbool.h>

void test_type(void) {
    char a;
    short b;
    int c;
    long d;
    long long e;
    short int f;
    long int g;
    long long int f;
    long int long g;
    float h;
    double i;
    long double j;
    _Bool k;
    bool l;
}

void test_signed(void) {
    signed char a;
    signed short b;
    signed int c;
    signed long d;
    signed long long e;
    signed short int f;
    signed long int g;
    signed long long int f;
}

void test_unsigned(void) {
    unsigned char a;
    unsigned short b;
    unsigned int c;
    unsigned long d;
    unsigned long long e;
    unsigned short int f;
    unsigned long int g;
    unsigned long long int f;
}

void test_storage_class(void) {
    static a;
    auto b;
    register c;
    static int d;
    auto int e;
    register int f;
}

void test_pointer(void) {
    int *a;
    expect(8, sizeof(a));
    int *b[5];
    expect(40, sizeof(b));
    int (*c)[5];
    expect(8, sizeof(c));
}

void test_unusual_order(void) {
    int unsigned auto * const * const a;
}

void test_typedef(void) {
    typedef int integer;
    integer a = 5;
    expect(5, a);

    typedef int array[3];
    array b = { 1, 2, 3 };
    expect(2, b[1]);

    typedef struct tag { int x; } strtype;
    strtype c;
    c.x = 5;
    expect(5, c.x);
}

void testmain(void) {
    print("type system");
    test_type();
    test_signed();
    test_unsigned();
    test_storage_class();
    test_pointer();
    test_unusual_order();
    test_typedef();
}
