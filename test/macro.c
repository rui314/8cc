#include "test/test.h"

void special(void) {
    expect_string("(stdin)", __FILE__);
    expect(5, __LINE__);
    expect(11, strlen(__DATE__));
    expect(8, strlen(__TIME__));
}

void include(void) {
#include "test/macro1.h"
    expect_string("macro1", MACRO_1);

#define MACRO_2_FILE "test/macro2.h"
#include MACRO_2_FILE
    expect_string("macro2", MACRO_2);

#define STDBOOL_H_FILE <stdbool.h>
#ifdef __STDBOOL_H
# error test failed
#endif
#include STDBOOL_H_FILE
#ifndef __STDBOOL_H
# error test failed
#endif
}

void predefined(void) {
    expect(1, __8cc__);
    expect(1, __amd64);
    expect(1, __amd64__);
    expect(1, __x86_64);
    expect(1, __x86_64__);
    expect(1, linux);
    expect(1, __linux);
    expect(1, __linux__);
    expect(1, __gnu_linux__);
    expect(1, __unix);
    expect(1, __unix__);
    expect(1, _LP64);
    expect(1, __LP64__);
    expect(1, __ELF__);
    expect(1, __STDC__);
    expect(1, __STDC_HOSTED__);
    expect(199901, __STDC_VERSION__);

    expect(2, __SIZEOF_SHORT__);
    expect(4, __SIZEOF_INT__);
    expect(8, __SIZEOF_LONG__);
    expect(8, __SIZEOF_LONG_LONG__);
    expect(4, __SIZEOF_FLOAT__);
    expect(8, __SIZEOF_DOUBLE__);
    expect(8, __SIZEOF_LONG_DOUBLE__);
    expect(8, __SIZEOF_POINTER__);
    expect(8, __SIZEOF_PTRDIFF_T__);
    expect(8, __SIZEOF_SIZE_T__);

    expect(sizeof(short), __SIZEOF_SHORT__);
    expect(sizeof(int), __SIZEOF_INT__);
    expect(sizeof(long), __SIZEOF_LONG__);
    expect(sizeof(long long), __SIZEOF_LONG_LONG__);
    expect(sizeof(float), __SIZEOF_FLOAT__);
    expect(sizeof(double), __SIZEOF_DOUBLE__);
    expect(sizeof(long double), __SIZEOF_LONG_DOUBLE__);
    expect(sizeof(void *), __SIZEOF_POINTER__);
    expect(sizeof(ptrdiff_t), __SIZEOF_PTRDIFF_T__);
    expect(sizeof(size_t), __SIZEOF_SIZE_T__);
}

#define ZERO 0
#define ONE 1
#define TWO ONE + ONE
#define LOOP LOOP

void simple(void) {
    expect(1, ONE);
    expect(2, TWO);
}

#define VAR1 VAR2
#define VAR2 VAR1

void loop(void) {
    int VAR1 = 1;
    int VAR2 = 2;
    expect(1, VAR1);
    expect(2, VAR2);
}

void undef(void) {
    int a = 3;
#define a 10
    expect(10, a);
#undef a
    expect(3, a);
#define a 16
    expect(16, a);
#undef a
}

void cond_incl(void) {
    int a = 1;
#if 0
    a = 2;
#endif
    expect(1, a);

#if 0
#elif 1
    a = 2;
#endif
    expect(2, a);

#if 1
    a = 3;
#elif 1
    a = 4;
#endif
    expect(3, a);

#if 1
    a = 5;
#endif
    expect(5, a);

#if 1
    a = 10;
#else
    a = 12;
#endif
    expect(10, a);

#if 0
    a = 11;
#else
    a = 12;
#endif
    expect(12, a);

#if 0
# if 1
# endif
#else
    a = 150;
#endif
    expect(150, a);
}

void const_expr(void) {
    int a = 1;
#if 0 + 1
    a = 2;
#else
    a = 3;
#endif
    expect(2, a);

#if 0 + 1 * 2 + 4 / 2 ^ 3 & ~1 % 5
    a = 4;
#else
    a = 5;
#endif
    expect(4, a);

#if 1 && 0
#else
    a = 100;
#endif
    expect(100, a);

#if 1 && 1
    a = 101;
#else
#endif
    expect(101, a);

#if 1 || 0
    a = 102;
#else
#endif
    expect(102, a);

#if 0 || 0
#else
    a = 103;
#endif
    expect(103, a);

#if 0
#elif !0
    a = 104;
#endif
    expect(104, a);

#if 0 + 0
    a = 6;
#else
    a = 7;
#endif
    expect(7, a);

#if ZERO
    a = 8;
#else
    a = 9;
#endif
    expect(9, a);

#if LOOP
    a = 10;
#else
    a = 11;
#endif
    expect(10, a);

#if LOOP - 1
    a = 12;
#else
    a = 13;
#endif
    expect(13, a);
}

void defined(void) {
    int a = 0;
#if defined ZERO
    a = 1;
#endif
    expect(1, a);
#if defined(ZERO)
    a = 2;
#endif
    expect(2, a);
#if defined(NO_SUCH_MACRO)
    a = 3;
#else
    a = 4;
#endif
    expect(4, a);
}

void ifdef(void) {
    int a = 0;
#ifdef ONE
    a = 1;
#else
    a = 2;
#endif
    expect(a, 1);

#ifdef NO_SUCH_MACRO
    a = 3;
#else
    a = 4;
#endif
    expect(a, 4);

#ifndef ONE
    a = 5;
#else
    a = 6;
#endif
    expect(a, 6);

#ifndef NO_SUCH_MACRO
    a = 7;
#else
    a = 8;
#endif
    expect(a, 7);
}

int plus(int a, int b) {
    return a + b;
}

int minus(int a, int b) {
    return a - b;
}

void funclike(void) {
#define m1(x) x
    expect(5, m1(5));
    expect(7, m1((5 + 2)));
    expect(8, m1(plus(5, 3)));
    expect(10, m1() 10);

#define m2(x) x + x
    expect(10, m2(5));

#define m3(x, y) x * y
    expect(50, m3(5, 10));
    expect(11, m3(2 + 2, 3 + 3));

#define m4(x, y) x + y + TWO
    expect(17, m4(5, 10));

#define m5(x) #x
    expect_string("5", m5(5));
    expect_string("x", m5(x));
    expect_string("x y", m5(x y));
    expect_string("x y", m5( x y ));
    expect_string("x + y", m5( x + y ));
    expect_string("x + y", m5(/**/x/**/+/**//**/ /**/y/**/));
    expect_string("x+y", m5( x+y ));
    expect_string("'a'", m5('a'));
    expect_string("'\\''", m5('\''));
    expect_string("\"abc\"", m5("abc"));
    expect_string("ZERO", m5(ZERO));

#define m6(x, ...) x + __VA_ARGS__
    expect(20, m6(2, 18));
    expect(25, plus(m6(2, 18, 5)));

#define plus(x, y) x * y + plus(x, y)
    expect(11, plus(2, 3));
#undef plus

#define plus(x, y)  minus(x, y)
#define minus(x, y) plus(x, y)
    expect(31, plus(30, 1));
    expect(29, minus(30, 1));

    // This is not a function-like macro.
#define m7 (0) + 1
    expect(1, m7);

#define m8(x, y) x ## y
    expect(2, m8(TW, O));

#define m9(x, y, z) x y + z
    expect(8, m9(1,, 7));

#define hash_hash # ## #
#define mkstr(a) # a
#define in_between(a) mkstr(a)
#define join(c, d) in_between(c hash_hash d)
    expect_string("x ## y", join(x, y));
}

void empty(void) {
#define EMPTY
    expect(1, 1 EMPTY);
#define EMPTY2(x)
    expect(2, 2 EMPTY2(foo));
    expect(2, 2 EMPTY2(foo bar));
    expect(2, 2 EMPTY2(((()))));
}

void noarg(void) {
#define NOARG() 55
    expect(55, NOARG());
}

void null(void) {
    #
}

void testmain(void) {
    print("macros");

    special();
    include();
    predefined();
    simple();
    loop();
    undef();
    cond_incl();
    const_expr();
    defined();
    ifdef();
    funclike();
    empty();
    noarg();
    null();
}
