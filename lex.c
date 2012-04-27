// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

static bool at_bol = true;

typedef struct {
    char *displayname;
    char *realname;
    int line;
    int column;
    FILE *fp;
} File;

static List *buffer = &EMPTY_LIST;
static List *altbuffer = NULL;
static List *file_stack = &EMPTY_LIST;
static File *file;
static int line_mark = -1;
static int column_mark = -1;
static int ungotten = -1;

static Token *newline_token = &(Token){ .type = TNEWLINE, .nspace = 0 };

static void skip_block_comment(void);

static File *make_file(char *displayname, char *realname, FILE *fp) {
    File *r = malloc(sizeof(File));
    r->displayname = displayname;
    r->realname = realname;
    r->line = 1;
    r->column = 0;
    r->fp = fp;
    return r;
}

void lex_init(char *filename) {
    if (!strcmp(filename, "-")) {
        set_input_file("(stdin)", NULL, stdin);
        return;
    }
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        char buf[128];
        strerror_r(errno, buf, sizeof(buf));
        error("Cannot open %s: %s", filename, buf);
    }
    set_input_file(filename, filename, fp);
}

static Token *make_token(Token *tmpl) {
    Token *r = malloc(sizeof(Token));
    *r = *tmpl;
    r->hideset = make_dict(NULL);
    r->file = file->displayname;
    r->line = (line_mark < 0) ? file->line : line_mark;
    r->column = (column_mark < 0) ? file->column : column_mark;
    line_mark = -1;
    column_mark = -1;
    return r;
}

static Token *make_ident(char *p) {
    return make_token(&(Token){ TIDENT, .nspace = 0, .sval = p });
}

static Token *make_strtok(char *s) {
    return make_token(&(Token){ TSTRING, .nspace = 0, .sval = s });
}

static Token *make_punct(int punct) {
    return make_token(&(Token){ TPUNCT, .nspace = 0, .punct = punct });
}

static Token *make_number(char *s) {
    return make_token(&(Token){ TNUMBER, .nspace = 0, .sval = s });
}

static Token *make_char(char c) {
    return make_token(&(Token){ TCHAR, .nspace = 0, .c = c });
}

static Token *make_space(int nspace) {
    return make_token(&(Token){ TSPACE, .nspace = nspace });
}

void push_input_file(char *displayname, char *realname, FILE *fp) {
    list_push(file_stack, file);
    file = make_file(displayname, realname, fp);
    at_bol = true;
}

void set_input_file(char *displayname, char *realname, FILE *fp) {
    file = make_file(displayname, realname, fp);
    at_bol = true;
}

char *input_position(void) {
    if (!file)
        return "(unknown)";
    return format("%s:%d:%d", file->displayname, file->line, file->column);
}

char *get_current_file(void) {
    return file->realname;
}

int get_current_line(void) {
    return file->line;
}

void set_current_line(int line) {
    file->line = line;
}

char *get_current_displayname(void) {
    return file->displayname;
}

void set_current_displayname(char *name) {
    file->displayname = name;
}

static void mark_input(void) {
    line_mark = file->line;
    column_mark = file->column;
}

static void unget(int c) {
    if (c == '\n')
        file->line--;
    if (ungotten >= 0)
        ungetc(ungotten, file->fp);
    ungotten = c;
    file->column--;
}

static bool skip_newline(int c) {
    if (c == '\n')
        return true;
    if (c == '\r') {
        int c2 = getc(file->fp);
        if (c2 == '\n')
            return true;
        ungetc(c2, file->fp);
        return true;
    }
    return false;
}

static int get(void) {
    int c = (ungotten >= 0) ? ungotten : getc(file->fp);
    file->column++;
    ungotten = -1;
    if (c == '\\') {
        c = getc(file->fp);
        file->column++;
        if (skip_newline(c)) {
            file->line++;
            file->column = 1;
            return get();
        }
        unget(c);
        at_bol = false;
        return '\\';
    }
    if (skip_newline(c)) {
        file->line++;
        file->column = 1;
        at_bol = true;
    } else {
        at_bol = false;
    }
    return c;
}

static int peek(void) {
    int r = get();
    unget(r);
    return r;
}

static bool next(int expect) {
    int c = get();
    if (c == expect)
        return true;
    unget(c);
    return false;
}

static void skip_line(void) {
    for (;;) {
        int c = get();
        if (c == EOF)
            return;
        if (c == '\n' || c == '\r') {
            unget(c);
            return;
        }
    }
}

static bool iswhitespace(int c) {
    return c == ' ' || c == '\t' || c == '\f' || c == '\v';
}

static int skip_space(void) {
    int nspace = 0;
    for (;;) {
        int c = get();
        if (c == EOF) break;
        if (iswhitespace(c)) {
            nspace++;
            continue;
        }
        if (c == '\t'){
            nspace += 4;
            continue;
        }
        if (c == '/') {
            if (next('*')) {
                skip_block_comment();
                nspace++;
                continue;
            } else if (next('/')) {
                skip_line();
                nspace++;
                continue;
            }
            unget('/');
            break;
        }
        unget(c);
        break;
    }
    return nspace;
}

void skip_cond_incl(void) {
    int nest = 0;
    for (;;) {
        skip_space();
        int c = get();
        if (c == EOF)
            return;
        if (c == '\n' || c == '\r')
            continue;
        if (c != '#') {
            skip_line();
            continue;
        }
        skip_space();
        Token *tok = read_cpp_token();
        if (tok->type == TNEWLINE)
            continue;
        if (tok->type != TIDENT) {
            skip_line();
            continue;
        }
        if (!nest && (is_ident(tok, "else") || is_ident(tok, "elif") || is_ident(tok, "endif"))) {
            unget_cpp_token(tok);
            Token *sharp = make_punct('#');
            sharp->bol = true;
            unget_cpp_token(sharp);
            return;
        }
        if (is_ident(tok, "if") || is_ident(tok, "ifdef") || is_ident(tok, "ifndef"))
            nest++;
        else if (nest && is_ident(tok, "endif"))
            nest--;
        skip_line();
    }
}

static Token *read_number(char c) {
    String *s = make_string();
    string_append(s, c);
    for (;;) {
        int c = get();
        if (!isdigit(c) && !isalpha(c) && c != '.') {
            unget(c);
            return make_number(get_cstring(s));
        }
        string_append(s, c);
    }
}

static int read_octal_char(int c) {
    int r = c - '0';
    c = get();
    if ('0' <= c && c <= '7') {
        r = (r << 3) | (c - '0');
        c = get();
        if ('0' <= c && c <= '7')
            r = (r << 3) | (c - '0');
        else
            unget(c);
    } else {
        unget(c);
    }
    return r;
}

static int read_hex_char(void) {
    int c = get();
    int r = 0;
    if (!isxdigit(c))
        error("\\x is not followed by a hexadecimal character: %c", c);
    for (;; c = get()) {
        switch (c) {
        case '0' ... '9': r = (r << 4) | (c - '0'); continue;
        case 'a' ... 'f': r = (r << 4) | (c - 'a' + 10); continue;
        case 'A' ... 'F': r = (r << 4) | (c - 'A' + 10); continue;
        default: unget(c); return r;
        }
    }
}

static int read_escaped_char(void) {
    int c = get();
    switch (c) {
    case '\'': case '"': case '?': case '\\':
        return c;
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'v': return '\v';
    case 'e': return '\033'; // '\e' is GNU extension
    case '0' ... '7':
        return read_octal_char(c);
    case 'x':
        return read_hex_char();
    case EOF:
        error("premature end of input");
    default:
        warn("unknown escape character: \\%c", c);
        return c;
    }
}

static Token *read_char(void) {
    int c = get();
    char r = (c == '\\') ? read_escaped_char() : c;
    int c2 = get();
    if (c2 != '\'')
        error("unterminated string: %c", c2);
    return make_char(r);
}

static Token *read_string(void) {
    String *s = make_string();
    for (;;) {
        int c = get();
        if (c == EOF)
            error("Unterminated string");
        if (c == '"')
            break;
        if (c == '\\')
            c = read_escaped_char();
        string_append(s, c);
    }
    return make_strtok(get_cstring(s));
}

static Token *read_ident(char c) {
    String *s = make_string();
    string_append(s, c);
    for (;;) {
        int c2 = get();
        if (isalnum(c2) || c2 == '_' || c2 == '$') {
            string_append(s, c2);
        } else {
            unget(c2);
            return make_ident(get_cstring(s));
        }
    }
}

static void skip_block_comment(void) {
    enum { in_comment, asterisk_read } state = in_comment;
    for (;;) {
        int c = get();
        if (c == EOF)
            error("premature end of block comment");
        if (c == '*')
            state = asterisk_read;
        else if (state == asterisk_read && c == '/')
            return;
        else
            state = in_comment;
    }
}

static Token *read_rep(char expect, int t1, int els) {
    if (next(expect))
        return make_punct(t1);
    return make_punct(els);
}

static Token *read_rep2(char expect1, int t1, char expect2, int t2, char els) {
    if (next(expect1))
        return make_punct(t1);
    if (next(expect2))
        return make_punct(t2);
    return make_punct(els);
}

static Token *read_token_int(void) {
    mark_input();
    int c = get();
    switch (c) {
    case ' ': case '\v': case '\f':
        return make_space(skip_space() + 1);
    case '\t':
        return make_space(skip_space() + 4);
    case '\n': case '\r':
        skip_newline(c);
        return newline_token;
    case 'L':
        if (next('"'))  return read_string();
        if (next('\'')) return read_char();
        return read_ident('L');
    case '0' ... '9':
        return read_number(c);
    case 'a' ... 'z': case 'A' ... 'K': case 'M' ... 'Z': case '_': case '$':
        return read_ident(c);
    case '/': {
        if (next('/')) {
            skip_line();
            return make_space(1);
        }
        if (next('*')) {
            skip_block_comment();
            return make_space(1);
        }
        if (next('='))
            return make_punct(OP_A_DIV);
        return make_punct('/');
    }
    case '.':
        if (isdigit(peek()))
            return read_number(c);
        if (next('.'))
            return make_ident(format("..%c", get()));
        return make_punct('.');
    case '(': case ')': case ',': case ';': case '[': case ']': case '{':
    case '}': case '?': case '~':
        return make_punct(c);
    case ':':
        return make_punct(next('>') ? ']' : ':');
    case '#': {
        if (next('#'))
            return make_ident("##");
        return make_punct('#');
    }
    case '+': return read_rep2('+', OP_INC, '=', OP_A_ADD, '+');
    case '-': {
        if (next('-'))
            return make_punct(OP_DEC);
        if (next('>'))
            return make_punct(OP_ARROW);
        if (next('='))
            return make_punct(OP_A_SUB);
        return make_punct('-');
    }
    case '*': return read_rep('=', OP_A_MUL, '*');
    case '%':
        if (next('>'))
            return make_punct('}');
        if (next(':')) {
            if (next('%')) {
                if (next(':'))
                    return make_ident("##");
                unget('%');
            }
            return make_punct('#');
        }
        return read_rep('=', OP_A_MOD, '%');
    case '=': return read_rep('=', OP_EQ, '=');
    case '!': return read_rep('=', OP_NE, '!');
    case '&': return read_rep2('&', OP_LOGAND, '=', OP_A_AND, '&');
    case '|': return read_rep2('|', OP_LOGOR, '=', OP_A_OR, '|');
    case '^': return read_rep('=', OP_A_XOR, '^');
    case '<': {
        if (next('<')) return read_rep('=', OP_A_SAL, OP_SAL);
        if (next('=')) return make_punct(OP_LE);
        if (next(':')) return make_punct('[');
        if (next('%')) return make_punct('{');
        return make_punct('<');
    }
    case '>': {
        if (next('='))
            return make_punct(OP_GE);
        if (next('>'))
            return read_rep('=', OP_A_SAR, OP_SAR);
        return make_punct('>');
    }
    case '"': return read_string();
    case '\'': return read_char();
    case EOF:
        return NULL;
    default:
        error("Unexpected character: '%c'", c);
    }
}

char *read_header_file_name(bool *std) {
    skip_space();
    char close;
    if (next('"')) {
        *std = false;
        close = '"';
    } else if (next('<')) {
        *std = true;
        close = '>';
    } else {
        return NULL;
    }
    String *s = make_string();
    for (;;) {
        int c = get();
        if (c == EOF || c == '\n' || c == '\r')
            error("premature end of header name");
        if (c == close)
            break;
        string_append(s, c);
    }
    if (get_cstring(s)[0] == '\0')
        error("header name should not be empty");
    return get_cstring(s);
}

bool is_punct(Token *tok, int c) {
    return tok && (tok->type == TPUNCT) && (tok->punct == c);
}

void set_input_buffer(List *tokens) {
    altbuffer = tokens ? list_reverse(tokens) : NULL;
}

List *get_input_buffer(void) {
    return altbuffer ? list_reverse(altbuffer) : NULL;
}

char *read_error_directive(void) {
    String *s = make_string();
    bool bol = true;
    for (;;) {
        int c = get();
        if (c == EOF) break;
        if (c == '\n' || c == '\r') {
            unget(c);
            break;
        }
        if (bol && iswhitespace(c)) continue;
        bol = false;
        string_append(s, c);
    }
    return get_cstring(s);
}

void unget_cpp_token(Token *tok) {
    if (!tok) return;
    list_push(altbuffer ? altbuffer : buffer, tok);
}

Token *peek_cpp_token(void) {
    Token *tok = read_token();
    unget_cpp_token(tok);
    return tok;
}

static Token *read_cpp_token_int(void) {
    if (altbuffer)
        return list_pop(altbuffer);
    if (list_len(buffer) > 0)
        return list_pop(buffer);
    bool bol = at_bol;
    Token *tok = read_token_int();
    while (tok && tok->type == TSPACE) {
        Token *tok2 = read_token_int();
        if (tok2)
            tok2->nspace += tok->nspace;
        tok = tok2;
    }
    if (!tok && list_len(file_stack) > 0) {
        fclose(file->fp);
        file = list_pop(file_stack);
        at_bol = true;
        return newline_token;
    }
    if (tok) tok->bol = bol;
    return tok;
}

Token *read_cpp_token(void) {
    return read_cpp_token_int();
}
