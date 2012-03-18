int expect(int a, int b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

#define ONE 1
#define TWO ONE + ONE

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
}

int main() {
    printf("Testing macros ... ");

    simple();
    loop();
    undef();
    cond_incl();
    const_expr();

    printf("OK\n");
    return 0;
}
