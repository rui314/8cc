int main() {
    expect(1, sizeof(char));
    expect(2, sizeof(short));
    expect(4, sizeof(int));
    expect(8, sizeof(long));

    expect(8, sizeof(char *));
    expect(8, sizeof(short *));
    expect(8, sizeof(int *));
    expect(8, sizeof(long *));

    expect(1, sizeof(unsigned char));
    expect(2, sizeof(unsigned short));
    expect(4, sizeof(unsigned int));
    expect(8, sizeof(unsigned long));

    expect(4, sizeof 1);
    expect(8, sizeof 1L);
    expect(8, sizeof 1.0);

    expect(1, sizeof(char[1]));
    expect(7, sizeof(char[7]));
    expect(30, sizeof(char[3][10]));
    expect(32, sizeof(int[4][2]));
}
