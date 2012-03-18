#include "test/include/test.h"

int expect(int a, int b) {
    if (!(a == b)) {
        printf("Failed\n");
        printf("  %d expected, but got %d\n", a, b);
        exit(1);
    }
}

int main() {
    printf("Testing inclusion ... ");

    expect(1, include_test_var1);
    expect(2, include_test_var2);

    printf("OK\n");
    return 0;
}
