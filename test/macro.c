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

int main() {
    printf("Testing macros ... ");

    simple();
    loop();
    undef();

    printf("OK\n");
    return 0;
}
