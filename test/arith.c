int test_basic() {
    expect(0, 0);
    expect(3, 1 + 2);
    expect(3, 1 + 2);
    expect(10, 1 + 2 + 3 + 4);
    expect(11, 1 + 2 * 3 + 4);
    expect(14, 1 * 2 + 3 * 4);
    expect(4, 4 / 2 + 6 / 3);
    expect(4, 24 / 2 / 3);
    expect(98, 'a' + 1);
    int a = 0 - 1;
    expect(0 - 1, a);
    expect(-1, a);
    expect(0, a + 1);
}

int test_inc_dec() {
    int a = 15;
    expect(15, a++);
    expect(16, a);
    expect(16, a--);
    expect(15, a);
}

int test_bool() {
    expect(0, !1);
    expect(1 ,!0);
}

int test_ternary() {
    expect(51, (1 + 2) ? 51 : 52);
    expect(52, (1 - 1) ? 51 : 52);
    expect(26, (1 - 1) ? 51 : 52 / 2);
    expect(17, (1 - 0) ? 51 / 3 : 52);
}

int test_logand() {
    expect(1, 55 && 2);
    expect(0, 55 && 0);
    expect(0, 0 && 55);
}

int test_bitand() {
    expect(3, 1 | 2);
    expect(1, 1 & 3);
}

int main() {
    printf("Testing basic arithmetic ... ");

    test_basic();
    test_inc_dec();
    test_bool();
    test_ternary();
    test_logand();
    test_bitand();

    printf("OK\n");
    return 0;
}
