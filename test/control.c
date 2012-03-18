int testif1() { if (1) { return 'a';} return 0; }
int testif2() { if (0) { return 0;} return 'b'; }
int testif3() { if (1) { return 'c';} else { return 0; } return 0; }
int testif4() { if (0) { return 0;} else { return 'd'; } return 0; }
int testif5() { if (1) return 'e'; return 0; }
int testif6() { if (0) return 0; return 'f'; }
int testif7() { if (1) return 'g'; else return 0; return 0; }
int testif8() { if (0) return 0; else return 'h'; return 0; }
int testif9() { if (0+1) return 'i'; return 0; }
int testif10() { if (1-1) return 0; return 'j'; }

int testif() {
    expect('a', testif1());
    expect('b', testif2());
    expect('c', testif3());
    expect('d', testif4());
    expect('e', testif5());
    expect('f', testif6());
    expect('g', testif7());
    expect('h', testif8());
    expect('i', testif9());
    expect('j', testif10());
}

int testfor() {
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

int main() {
    printf("Testing control flow ... ");

    testif();
    testfor();

    printf("OK\n");
    return 0;
}
