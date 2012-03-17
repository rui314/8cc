int expect(int a, int b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

int main() {
    printf("Testing comparison operators ... ");

    expect(1, 1 < 2);
    expect(0, 2 < 1);
    expect(1, 1 == 1);
    expect(0, 1 == 2);

    printf("OK\n");
    return 0;
}
