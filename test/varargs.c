#include <stdarg.h>

void expect_string(char *a, char *b);
void expectf(float a, float b);
void expectd(double a, double b);

void test_int(int a, ...) {
    va_list ap;
    va_start(ap, a);
    expect(1, a);
    expect(2, va_arg(ap, int));
    expect(3, va_arg(ap, int));
    expect(5, va_arg(ap, int));
    expect(8, va_arg(ap, int));
    va_end(ap);
}

void test_float(float a, ...) {
    va_list ap;
    va_start(ap, a);
    expectf(1.0, a);
    expectf(2.0, va_arg(ap, float));
    expectf(4.0, va_arg(ap, float));
    expectf(8.0, va_arg(ap, float));
    va_end(ap);
}

#include <stdio.h>

void test_mix(char *p, ...) {
    va_list ap;
    va_start(ap, p);
    expect_string("abc", p);
    expectf(2.0, va_arg(ap, float));
    expect(4, va_arg(ap, int));
    expect_string("d", va_arg(ap, char *));
    expect(5, va_arg(ap, int));
    va_end(ap);
}

int main() {
    printf("Testing varargs ... ");

    test_int(1, 2, 3, 5, 8);
    test_float(1.0, 2.0, 4.0, 8.0);
    test_mix("abc", 2.0, 4, "d", 5);

    printf("OK\n");
    return 0;
}
