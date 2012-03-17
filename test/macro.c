int expect(int a, int b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

#define ONE 1
#define TWO ONE + ONE

int main() {
    printf("Testing macros ... ");

    expect(1, ONE);
    expect(2, TWO);

    printf("OK\n");
    return 0;
}
