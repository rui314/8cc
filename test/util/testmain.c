#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void testmain(void);

int externvar1 = 98;
int externvar2 = 99;

void print(char *s) {
    printf("Testing %s ... ", s);
    fflush(stdout);
}

void fail(char *msg) {
    printf("Failed: %s\n", msg);
    exit(1);
}

void expect(int a, int b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

void expect_string(char *a, char *b) {
    if (strcmp(a, b)) {
        printf("Failed\n");
        printf("  \"%s\" expected, but got \"%s\"\n", a, b);
        exit(1);
    }
}

void expectf(float a, float b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %f expected, but got %f\n", a, b);
        exit(1);
    }
}

void expectd(double a, double b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %lf expected, but got %lf\n", a, b);
        exit(1);
    }
}

int main() {
    testmain();
    printf("OK\n");
}
