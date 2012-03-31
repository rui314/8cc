#include "test/test.h"

void testmain() {
    print("literal");

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

    expect_string("abc", "abc");
    expect_string('a', "abc"[0]);
    expect_string(0, "abc"[4]);

    expect_string("c", L'c');
    expect_string("asdf", L"asdf");

    // make sure we can handle an identifier starting with "L"
    int L = 7;
    expect(7, L);
    int L123 = 123;
    expect(123, L123);
}
