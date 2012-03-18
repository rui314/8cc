int t1() {
    int a = 1;
    expect(3, a + 2);
}

int t2() {
    int a = 1;
    int b = 48 + 2;
    int c = a + b;
    expect(102, c * 2);
}

int t3() {
    int a[] = { 55 };
    int *b = a;
    expect(55, *b);
}

int t4() {
    int a[] = { 55, 67 };
    int *b = a + 1;
    expect(67, *b);
}

int t5() {
    int a[] = { 20, 30, 40 };
    int *b = a + 1;
    expect(30, *b);
}

int t6() {
    int a[] = { 20, 30, 40 };
    expect(20, *a);
}

int main() {
    printf("Testing declaration ... ");

    t1();
    t2();
    t3();
    t4();
    t5();
    t6();

    printf("OK\n");
    return 0;
}
