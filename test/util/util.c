#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
