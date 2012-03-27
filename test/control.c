int test_if1() { if (1) { return 'a';} return 0; }
int test_if2() { if (0) { return 0;} return 'b'; }
int test_if3() { if (1) { return 'c';} else { return 0; } return 0; }
int test_if4() { if (0) { return 0;} else { return 'd'; } return 0; }
int test_if5() { if (1) return 'e'; return 0; }
int test_if6() { if (0) return 0; return 'f'; }
int test_if7() { if (1) return 'g'; else return 0; return 0; }
int test_if8() { if (0) return 0; else return 'h'; return 0; }
int test_if9() { if (0+1) return 'i'; return 0; }
int test_if10() { if (1-1) return 0; return 'j'; }

void test_if() {
    expect('a', test_if1());
    expect('b', test_if2());
    expect('c', test_if3());
    expect('d', test_if4());
    expect('e', test_if5());
    expect('f', test_if6());
    expect('g', test_if7());
    expect('h', test_if8());
    expect('i', test_if9());
    expect('j', test_if10());
}

void test_for() {
    int i;
    int acc = 0;
    for (i = 0; i < 5; i++) {
        acc = acc + i;
    }
    expect(10, acc);

    acc = 0;
    for (i = 0; i < 5; i++)
        acc = acc + i;
    expect(10, acc);

    acc = 0;
    for (i = 0; i < 100; i++) {
        if (i < 5) continue;
        if (i == 9) break;
        acc += i;
    }
    expect(5 + 6 + 7 + 8, acc);
}

void test_while() {
    int acc = 0;
    int i = 0;
    while (i <= 100)
        acc = acc + i++;
    expect(5050, acc);

    acc = 1;
    i = 0;
    while (i <= 100) {
        acc = acc + i++;
    }
    expect(5051, acc);

    acc = 0;
    i = 0;
    while (i < 10) {
        if (i++ < 5) continue;
        acc += i;
        if (i == 9) break;
    }
    expect(6 + 7 + 8 + 9, acc);
}

void test_do() {
    int acc = 0;
    int i = 0;
    do {
        acc = acc + i++;
    } while (i <= 100);
    expect(5050, acc);

    i = 0;
    do { i = 37; } while (0);
    expect(37, i);

    acc = 0;
    i = 0;
    do {
        if (i++ < 5) continue;
        acc += i;
        if (i == 9) break;
    } while (i < 10);
    expect(6 + 7 + 8 + 9, acc);
}

void test_switch() {
    int a = 0;
    switch (1+2) {
    case 0: fail("0");
    case 3: a = 3; break;
    case 1: fail("1");
    }
    expect(a, 3);

    a = 0;
    switch (1) {
    case 0: a++;
    case 1: a++;
    case 2: a++;
    case 3: a++;
    }
    a = 3;

    a = 0;
    switch (100) {
    case 0: a++;
    default: a = 55;
    }
    expect(a, 55);

    a = 0;
    switch (100) {
    case 0: a++;
    }
    expect(a, 0);

    a = 5;
    switch (3) {
        a++;
    }
    expect(a, 5);
}

int main() {
    printf("Testing control flow ... ");

    test_if();
    test_for();
    test_while();
    test_do();
    test_switch();

    printf("OK\n");
    return 0;
}
