int expect(int a, int b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

int expect_string(char *a, char *b) {
    if (strcmp(a, b)) {
        printf("Failed\n");
        printf("  \"%s\" expected, but got \"%s\"\n", a, b);
        exit(1);
    }
}

#define ZERO 0
#define ONE 1
#define TWO ONE + ONE
#define LOOP LOOP

int simple() {
    expect(1, ONE);
    expect(2, TWO);
}

#define VAR1 VAR2
#define VAR2 VAR1

int loop() {
    int VAR1 = 1;
    int VAR2 = 2;
    expect(1, VAR1);
    expect(2, VAR2);
}

int undef() {
    int a = 3;
#define a 10
    expect(10, a);
#undef a
    expect(3, a);
#define a 16
    expect(16, a);
#undef a
}

int cond_incl() {
    int a = 1;
#if 0
    a = 5;
#endif
    expect(1, a);

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
}

int const_expr() {
    int a = 1;
#if 0 + 1
    a = 2;
#else
    a = 3;
#endif
    expect(2, a);

#if 0 + 1 * 2 + 4 / 2
    a = 4;
#else
    a = 5;
#endif
    expect(4, a);

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

int defined_op() {
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

int plus(int a, int b) {
    return a + b;
}

int minus(int a, int b) {
    return a - b;
}

int funclike() {
#define m1(x) x
    expect(5, m1(5));

#define m2(x) x + x
    expect(10, m2(5));

#define m3(x, y) x + y
    expect(15, m3(5, 10));

#define m4(x, y) x + y + TWO
    expect(17, m4(5, 10));

#define m5(x) #x
    expect_string("5", m5(5));
    expect_string("x", m5(x));
    expect_string("x y", m5(x y));
    expect_string("x y", m5( x y ));
    expect_string("x + y", m5( x + y ));
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
    int x = 3;
#define m7 (x) + x
    expect(6, m7);
}

int main() {
    printf("Testing macros ... ");

    simple();
    loop();
    undef();
    cond_incl();
    const_expr();
    defined_op();
    funclike();

    printf("OK\n");
    return 0;
}
