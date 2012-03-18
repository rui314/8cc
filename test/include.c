#include "test/include/test.h"

int main() {
    printf("Testing inclusion ... ");

    expect(1, include_test_var1);
    expect(2, include_test_var2);

    printf("OK\n");
    return 0;
}
