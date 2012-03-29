#include "test/test.h"

void t1() {
    int a = 61;
    int *b = &a;
    expect(61, *b);
}

void t2() {
    char *c = "ab";
    expect(97, *c);
}

void t3() {
    char *c = "ab" + 1;
    expect(98, *c);
}

void t4() {
    char s[] = "xyz";
    char *c = s + 2;
    expect(122, *c);
}

void t5() {
    char s[] = "xyz";
    *s = 65;
    expect(65, *s);
}

int main() {
    printf("Testing pointer ... ");

    t1();
    t2();
    t3();
    t4();
    t5();

    printf("OK\n");
    return 0;
}
