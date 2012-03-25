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

int test_if() {
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

int test_for() {
    int i;
    int acc = 0;
    for (i = 0; i < 5; i = i + 1) {
        acc = acc + i;
    }
    expect(10, acc);

    acc = 0;
    for (i = 0; i < 5; i = i + 1)
        acc = acc + i;
    expect(10, acc);
}

int test_while() {
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
}

int test_do() {
    int acc = 0;
    int i = 0;
    do {
        acc = acc + i++;
    } while (i <= 100);
    expect(5050, acc);

    i = 0;
    do { i = 37; } while (0);
    expect(37, i);
}

int main() {
    printf("Testing control flow ... ");

    test_if();
    test_for();
    test_while();
    test_do();

    printf("OK\n");
    return 0;
}
