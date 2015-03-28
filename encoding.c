// Copyright 2015 Rui Ueyama. Released under the MIT license.

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
    int len = count_leading_ones(s[0]);
    if (len == 0) {
        *r = s[0];
        return 1;
    }
    if (s + len > end)
        error("invalid UTF-8 sequence");
    for (int i = 1; i < len; i++)
        if ((s[i] & 0xC0) != 0x80)
            error("invalid UTF-8 continuation byte");
    switch (len) {
    case 2:
        *r = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    case 3:
        *r = ((s[0] & 0xF) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    case 4:
        *r = ((s[0] & 0x7) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    error("invalid UTF-8 sequence");
}

static void write16(Buffer *b, uint16_t x) {
    buf_write(b, x & 0xFF);
    buf_write(b, x >> 8);
}

static void write32(Buffer *b, uint32_t x) {
    write16(b, x & 0xFFFF);
    write16(b, x >> 16);
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

void write_utf8(Buffer *b, uint32_t rune) {
    if (rune < 0x80) {
        buf_write(b, rune);
        return;
    }
    if (rune < 0x800) {
        buf_write(b, 0xC0 | (rune >> 6));
        buf_write(b, 0x80 | (rune & 0x3F));
        return;
    }
    if (rune < 0x10000) {
        buf_write(b, 0xE0 | (rune >> 12));
        buf_write(b, 0x80 | ((rune >> 6) & 0x3F));
        buf_write(b, 0x80 | (rune & 0x3F));
        return;
    }
    if (rune < 0x200000) {
        buf_write(b, 0xF0 | (rune >> 18));
        buf_write(b, 0x80 | ((rune >> 12) & 0x3F));
        buf_write(b, 0x80 | ((rune >> 6) & 0x3F));
        buf_write(b, 0x80 | (rune & 0x3F));
        return;
    }
    error("invalid UCS character: \\U%08x", rune);
}
