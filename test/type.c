int test_type() {
    char a;
    short b;
    int c;
    long d;
    long long e;
    short int f;
    long int g;
    long long int f;
    long int long g;
    float h;
    double i;
    long double j;
}

int test_signed() {
    signed char a;
    signed short b;
    signed int c;
    signed long d;
    signed long long e;
    signed short int f;
    signed long int g;
    signed long long int f;
}

int test_unsigned() {
    unsigned char a;
    unsigned short b;
    unsigned int c;
    unsigned long d;
    unsigned long long e;
    unsigned short int f;
    unsigned long int g;
    unsigned long long int f;
}

int test_storage_class() {
    static a;
    auto b;
    register c;
    static int d;
    auto int e;
    register int f;
}

int test_pointer() {
    int *a;
    expect(8, sizeof(a));
    int *b[5];
    expect(40, sizeof(b));
    int (*c)[5];
    expect(8, sizeof(c));
}

int test_unusual_order() {
    int unsigned auto * const * const a;
}

int test_typedef() {
    typedef int integer;
    integer a = 5;
    expect(5, a);

    typedef int array[3];
    array b = { 1, 2, 3 };
    expect(2, b[1]);

    typedef struct tag { int x; } strtype;
    strtype c;
    c.x = 5;
    expect(5, c.x);
}

int main() {
    printf("Testing type system ... ");

    test_type();
    test_signed();
    test_unsigned();
    test_storage_class();
    test_pointer();
    test_unusual_order();
    test_typedef();

    printf("OK\n");
    return 0;
}
