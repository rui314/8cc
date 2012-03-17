int val = 21;
int a1[3];
int a2[3] = { 24, 25, 26 };

int expect(int a, int b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

int main() {
    printf("Testing global variable ... ");

    expect(21, val);
    val = 22;
    expect(22, val);
    
    a1[1] = 23;
    expect(23, a1[1]);
    expect(25, a2[1]);

    printf("OK\n");
    return 0;
}
