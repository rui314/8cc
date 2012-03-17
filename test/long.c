int expect(long a, long b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %ld expected, but got %ld\n", a, b);
        exit(1);
    }
}

int main() {
    printf("Testing long ... ");

    expect(10L, 10L);
    expect(1152921504606846976, 1152921504606846976);
    expect(1152921504606846977, 1152921504606846976 + 1);

    printf("OK\n");
    return 0;
}
