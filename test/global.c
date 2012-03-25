int val = 21;
int a1[3];
int a2[3] = { 24, 25, 26 };
int x1, x2;
int x3, x4 = 4;
int x5 = 5, x6;

int main() {
    printf("Testing global variable ... ");

    expect(21, val);
    val = 22;
    expect(22, val);

    a1[1] = 23;
    expect(23, a1[1]);
    expect(25, a2[1]);

    x1 = 1;
    x2 = 2;
    expect(1, x1);
    expect(2, x2);
    x3 = 3;
    expect(3, x3);
    expect(4, x4);
    expect(5, x5);
    x6 = 6;
    expect(6, x6);

    printf("OK\n");
    return 0;
}
