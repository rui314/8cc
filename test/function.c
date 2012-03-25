int t1() {
    return 77;
}

int t2(int a) {
    expect(79, a);
}

int t3(int a, int b, int c, int d, int e, int f) {
    expect(1, a);
    expect(2, b);
    expect(3, c);
    expect(4, d);
    expect(5, e);
    expect(6, f);
}

int t4a(int *p) {
    return *p;
}

int t4() {
    int a[] = { 98 };
    expect(98, t4a(a));
}

int t5a(int *p) {
    expect(99, *p); p=p+1;
    expect(98, *p); p=p+1;
    expect(97, *p);
}

int t5b(int p[]) {
    expect(99, *p); p=p+1;
    expect(98, *p); p=p+1;
    expect(97, *p);
}

int t5() {
    int a[] = {1, 2, 3};
    int *p = a;
    *p = 99; p = p + 1;
    *p = 98; p = p + 1;
    *p = 97;
    t5a(a);
    t5b(a);
}

int t6();
int t6() {
    return 3;
}

int t7(int a, int b);
int t7(int a, int b) {
    return a * b;
}

int t8(int a, ...) {
    expect(23, a);
}

void t9() {
    return;
}

int main() {
    printf("Testing function ... ");

    expect(77, t1());
    t2(79);
    t3(1, 2, 3, 4, 5, 6);
    t4();
    t5();
    expect(3, t6());
    expect(12, t7(3, 4));
    t8(23);
    t9();

    printf("OK\n");
    return 0;
}
