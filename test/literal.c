// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "test.h"
#include "string.h"

static void test_char() {
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
    expect(15, '\xF');
    expect(18, '\x012');
}

static void test_string() {
    expect_string("abc", "abc");
    expect_string("abc", u8"abc");
    expect('a', "abc"[0]);
    expect(0, "abc"[3]);
    expect_string("abcd", "ab" "cd");
    expect_string("abcdef", "ab" "cd" "ef");

    char expected[] = { 65, 97, 7, 8, 12, 10, 13, 9, 11, 27, 7, 15, -99, -1, 18, 0 };
    expect_string(expected, "Aa\a\b\f\n\r\t\v\e\7\17\235\xff\x012");
    expect('c', L'c');
    expect(0x3042, L'\u3042');
    expect(0x3042, u'\u3042');
    expect(0x3042, U'\u3042');

    // Make sure we can handle an identifier starting with "L", "u", "U" or "u8".
    int L = 1, u = 2, U = 3, u8 = 4;
    expect(10, L + u + U + u8);
    int Lx = 1, ux = 2, Ux = 3, u8x = 4;
    expect(10, Lx + ux + Ux + u8x);
}

static void test_mbstring() {
    expect(2, sizeof(u""));
    expect(8, sizeof(u"abc"));
    expect(8, sizeof("ab" u"c"));
    expect(8, sizeof(u"ab" u"c"));
    expect(1, sizeof(u8""));
    expect(4, sizeof(u8"abc"));
    expect(4, sizeof("ab" u8"c"));
    expect(4, sizeof(u8"ab" u8"c"));
    expect(4, sizeof(L""));
    expect(16, sizeof(L"abc"));
    expect(16, sizeof(L"ab" L"c"));
    expect(4, sizeof(U""));
    expect(16, sizeof(U"abc"));
    expect(16, sizeof("ab" U"c"));
    expect(16, sizeof(U"ab" U"c"));
    expect(0, memcmp("x\0\0\0y\0\0\0z\0\0\0\0\0\0", L"xyz", 16));
    expect(0, memcmp("x\0\0\0y\0\0\0z\0\0\0\0\0\0", U"xyz", 16));
    expect(0, memcmp("\x78\0\x79\0\x7A\0\0\0", u"xyz", 8));

    expect(4, sizeof("\u3042"));
    expect(0, memcmp("\xE3\x81\x82\0", "\u3042", 4));
    expect(12, sizeof("\u3042" L"x"));
    expect(0, memcmp("\x42\x30\0\0\x78\0\0\0\0\0\0\0", "\u3042" L"x", 12));

    // GCC 5 allows UTF-8 strings as identifiers.
#ifdef __8cc__
    int 日本語 = 3;
    expect(3, 日本語);
    expect(3, 日\u672C\U00008A9E);
#endif
}

static void test_float() {
    expectf(1.0, 1.0);
    expectd(1.0, 1.0L);
    expectf(1.0, 0x1p+0);
    expectf(1.0, 0x1p-0);
}

static void test_ucn() {
    expect('$', L'\u0024');
    expect('$', L'\U00000024');
    expect_string("$", "\u0024");
    expect_string("$", "\U00000024");
    expect('X', L'X');
    expect('X', U'X');
    expect('X', u'X');
}

int g1 = 80;
int *g2 = &(int){ 81 };
struct g3 { int x; } *g3 = &(struct g3){ 82 };
struct g4 { char x; struct g4a { int y[2]; } *z; } *g4 = &(struct g4){ 83, &(struct g4a){ 84, 85 } };

static void test_compound() {
    expect(1, (int){ 1 });
    expect(3, ((int[]){ 1, 2, 3 }[2]));
    expect(12, sizeof((int[]){ 1, 2, 3 }));
    expect(6, ((struct { int x[3]; }){ 5, 6, 7 }.x[1]));

    expect(80, g1);
    expect(81, *g2);
    expect(82, g3->x);
    expect(83, g4->x);
    expect(84, g4->z->y[0]);
    expect(85, g4->z->y[1]);
}

void testmain() {
    print("literal");
    test_char();
    test_string();
    test_mbstring();
    test_float();
    test_ucn();
    test_compound();
}
