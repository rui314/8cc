int main() {
    printf("Testing character literal ... ");

    expect(65, 'A');
    expect(97, 'a');
    expect(7, '\a');
    expect(8, '\b');
    expect(12, '\f');
    expect(10, '\n');
    expect(13, '\r');
    expect(9, '\t');
    expect(11, '\v');
    expect(27, '\e');

    expect(0, '\0');
    expect(7, '\7');
    expect(15, '\17');
    expect(-99, '\235');

    expect(0, '\x0');
    expect(-1, '\xff');
    expect(18, '\x012');

    printf("OK\n");
    return 0;
}
