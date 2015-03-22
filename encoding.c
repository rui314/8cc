// Copyright 2015 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

// This file defines functions to convert UTF-8 strings to UTF-16 or UTF-32.
//
// 8cc uses UTF-16 for string literals prefixed with u (char16_t strings).
// UTF-32 is used for string literals prefixed with L or U
// (wchar_t or char32_t strings).
// Unprefixed or u8 strings are supposed to be in UTF-8 endcoding.
// Source files are supposed to be written in UTF-8.

#include "8cc.h"

static int count_leading_ones(char c) {
    for (int i = 7; i >= 0; i--)
        if ((c & (1 << i)) == 0)
            return 7 - i;
    return 8;
}

static int read_rune(uint32_t *r, char *s, char *end) {
    int len = count_leading_ones(s[0]) + 1;
    if (len > 5 || s + len > end)
        error("invalid UTF-8 sequence");
    if (len == 1) {
        *r = s[0];
        return 1;
    }
    *r = (len == 2) ? (s[0] & 0b11111) :
        (len == 3) ? (s[0] & 0b1111) :
        (len == 4) ? (s[0] & 0b111) : (s[0] & 0b11);
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0b11000000) != 0b10000000)
            error("invalid UTF-8 continuation byte");
        *r = (*r << 6) | (s[i] & 0b111111);
    }
    return len;
}

static void write16(Buffer *b, uint32_t rune) {
    buf_write(b, rune & 0xFF);
    buf_write(b, (rune >> 8) & 0xFF);
}

static void write32(Buffer *b, uint32_t rune) {
    buf_write(b, rune & 0xFF);
    buf_write(b, (rune >> 8) & 0xFF);
    buf_write(b, (rune >> 16) & 0xFF);
    buf_write(b, (rune >> 24) & 0xFF);
}

Buffer *to_utf16(char *p, int len) {
    Buffer *b = make_buffer();
    char *end = p + len;
    while (p != end) {
        uint32_t rune;
        p += read_rune(&rune, p, end);
        if (rune < 0x10000) {
            write16(b, rune);
        } else {
            write16(b, (rune >> 10) + 0xD7C0);
            write16(b, (rune & 0x3FF) + 0xDC00);
        }
    }
    return b;
}

Buffer *to_utf32(char *p, int len) {
    Buffer *b = make_buffer();
    char *end = p + len;
    while (p != end) {
        uint32_t rune;
        p += read_rune(&rune, p, end);
        write32(b, rune);
    }
    return b;
}
