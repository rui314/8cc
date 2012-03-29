#include "test/test.h"

void expectf(float a, float b);
void expectd(double a, double b);

int main() {
    printf("Testing cast ... ");

    expect(1, (int)1);
    expectf(1.0, (float)1);
    expectd(2.0, (double)2);

    printf("OK\n");
    return 0;
}
