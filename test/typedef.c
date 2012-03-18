int main() {
    printf("Testing typedef ... ");

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

    printf("OK\n");
    return 0;
}
