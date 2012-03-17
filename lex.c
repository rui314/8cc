#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "8cc.h"

static Token *ungotten = NULL;

static Token *make_ident(String *s) {
    Token *r = malloc(sizeof(Token));
    r->type = TTYPE_IDENT;
    r->sval = get_cstring(s);
    return r;
}

static Token *make_strtok(String *s) {
    Token *r = malloc(sizeof(Token));
    r->type = TTYPE_STRING;
    r->sval = get_cstring(s);
    return r;
}

static Token *make_punct(int punct) {
    Token *r = malloc(sizeof(Token));
    r->type = TTYPE_PUNCT;
    r->punct = punct;
    return r;
}

static Token *make_number(char *s) {
    Token *r = malloc(sizeof(Token));
    r->type = TTYPE_NUMBER;
    r->sval = s;
    return r;
}

static Token *make_char(char c) {
    Token *r = malloc(sizeof(Token));
    r->type = TTYPE_CHAR;
    r->c = c;
    return r;
}

static int getc_nonspace(void) {
    int c;
    while ((c = getc(stdin)) != EOF) {
        if (isspace(c) || c == '\n' || c == '\r')
            continue;
        return c;
    }
    return EOF;
}

static Token *read_number(char c) {
    String *s = make_string();
    string_append(s, c);
    for (;;) {
        int c = getc(stdin);
        if (!isdigit(c) && !isalpha(c) && c != '.') {
            ungetc(c, stdin);
            return make_number(get_cstring(s));
        }
        string_append(s, c);
    }
}

static Token *read_char(void) {
    char c = getc(stdin);
    if (c == EOF) goto err;
    if (c == '\\') {
        c = getc(stdin);
        if (c == EOF) goto err;
    }
    char c2 = getc(stdin);
    if (c2 == EOF) goto err;
    if (c2 != '\'')
        error("Malformed char literal");
    return make_char(c);
 err:
    error("Unterminated char");
}

static Token *read_string(void) {
    String *s = make_string();
    for (;;) {
        int c = getc(stdin);
        if (c == EOF)
            error("Unterminated string");
        if (c == '"')
            break;
        if (c == '\\') {
            c = getc(stdin);
            switch (c) {
            case EOF: error("Unterminated \\");
            case '\"': break;
            case 'n': c = '\n'; break;
            default: error("Unknown quote: %c", c);
            }
        }
        string_append(s, c);
    }
    return make_strtok(s);
}

static Token *read_ident(char c) {
    String *s = make_string();
    string_append(s, c);
    for (;;) {
        int c2 = getc(stdin);
        if (isalnum(c2) || c2 == '_') {
            string_append(s, c2);
        } else {
            ungetc(c2, stdin);
            return make_ident(s);
        }
    }
}

static void skip_line_comment(void) {
    for (;;) {
        int c = getc(stdin);
        if (c == '\n' || c == EOF)
            return;
    }
}

static void skip_block_comment(void) {
    enum { in_comment, asterisk_read } state = in_comment;
    for (;;) {
        int c = getc(stdin);
        if (state == in_comment) {
            if (c == '*')
                state = asterisk_read;
        } else if (c == '/') {
            return;
        }
    }
}

static Token *read_rep(int expect, int t1, int t2) {
    int c = getc(stdin);
    if (c == expect)
        return make_punct(t2);
    ungetc(c, stdin);
    return make_punct(t1);
}

static Token *read_token_int(void) {
    int c = getc_nonspace();
    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return read_number(c);
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z': case 'A': case 'B':
    case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I':
    case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
    case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W':
    case 'X': case 'Y': case 'Z': case '_':
        return read_ident(c);
    case '/': {
        c = getc(stdin);
        if (c == '/') {
            skip_line_comment();
            return read_token_int();
        }
        if (c == '*') {
            skip_block_comment();
            return read_token_int();
        }
        ungetc(c, stdin);
        return make_punct('/');
    }
    case '*': case '(': case ')': case ',': case ';': case '.': case '[':
    case ']': case '{': case '}': case '<': case '>': case '!': case '?':
    case ':':
        return make_punct(c);
    case '-':
        c = getc(stdin);
        if (c == '-') return make_punct(PUNCT_DEC);
        if (c == '>') return make_punct(PUNCT_ARROW);
        ungetc(c, stdin);
        return make_punct('-');
    case '=': return read_rep('=', '=', PUNCT_EQ);
    case '+': return read_rep('+', '+', PUNCT_INC);
    case '&': return read_rep('&', '&', PUNCT_LOGAND);
    case '|': return read_rep('|', '|', PUNCT_LOGOR);
    case '"': return read_string();
    case '\'': return read_char();
    case EOF:
        return NULL;
    default:
        error("Unexpected character: '%c'", c);
    }
}

bool is_punct(Token *tok, int c) {
    return tok && (tok->type == TTYPE_PUNCT) && (tok->punct == c);
}

void unget_token(Token *tok) {
    if (!tok) return;
    if (ungotten)
        error("Push back buffer is already full");
    ungotten = tok;
}

Token *peek_token(void) {
    Token *tok = read_token();
    unget_token(tok);
    return tok;
}

Token *read_token(void) {
    if (ungotten) {
        Token *tok = ungotten;
        ungotten = NULL;
        return tok;
    }
    return read_token_int();
}
