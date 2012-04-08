#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

static bool at_bol = true;

typedef struct {
    char *name;
    int line;
    int column;
    FILE *fp;
} File;

static List *buffer = &EMPTY_LIST;
static List *altbuffer = NULL;
static List *file_stack = &EMPTY_LIST;
static File *file;
static int ungotten = -1;

static Token *newline_token = &(Token){ .type = TTYPE_NEWLINE, .space = false };
static Token *space_token = &(Token){ .type = TTYPE_SPACE, .space = false };

static void skip_block_comment(void);

static File *make_file(char *name, FILE *fp) {
    File *r = malloc(sizeof(File));
    r->name = name;
    r->line = 1;
    r->column = 0;
    r->fp = fp;
    return r;
}

void lex_init(char *filename) {
    if (!strcmp(filename, "-")) {
        set_input_file("(stdin)", stdin);
        return;
    }
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        char buf[128];
        strerror_r(errno, buf, sizeof(buf));
        error("Cannot open %s: %s", filename, buf);
    }
    set_input_file(filename, fp);
}

static Token *make_token(Token *tmpl) {
    Token *r = malloc(sizeof(Token));
    *r = *tmpl;
    r->hideset = make_dict(NULL);
    r->file = file->name;
    r->line = file->line;
    r->column = file->column;
    return r;
}

static Token *make_ident(char *p) {
    return make_token(&(Token){ TTYPE_IDENT, .sval = p });
}

static Token *make_strtok(char *s) {
    return make_token(&(Token){ TTYPE_STRING, .sval = s });
}

static Token *make_punct(int punct) {
    return make_token(&(Token){ TTYPE_PUNCT, .punct = punct });
}

static Token *make_number(char *s) {
    return make_token(&(Token){ TTYPE_NUMBER, .sval = s });
}

static Token *make_char(char c) {
    return make_token(&(Token){ TTYPE_CHAR, .c = c });
}

void push_input_file(char *filename, FILE *fp) {
    list_push(file_stack, file);
    file = make_file(filename, fp);
    at_bol = true;
}

void set_input_file(char *filename, FILE *fp) {
    file = make_file(filename, fp);
    at_bol = true;
}

char *input_position(void) {
    return format("%s:%d:%d", file->name, file->line, file->column);
}

char *get_current_file(void) {
    return file->name;
}

int get_current_line(void) {
    return file->line;
}

static void unget(int c) {
    if (c == '\n')
        file->line--;
    if (ungotten >= 0)
        ungetc(ungotten, file->fp);
    ungotten = c;
    file->column--;
}

static int get(void) {
    int c = (ungotten >= 0) ? ungotten : getc(file->fp);
    file->column++;
    ungotten = -1;
    if (c == '\\') {
        c = getc(file->fp);
        file->column++;
        if (c == '\n') {
            file->line++;
            file->column = 1;
            return get();
        }
        unget(c);
        at_bol = false;
        return '\\';
    }
    if (c == '\n') {
        file->line++;
        file->column = 1;
        at_bol = true;
    } else {
        at_bol = false;
    }
    return c;
}

static void skip_line(void) {
    for (;;) {
        int c = get();
        if (c == EOF || c == '\n')
            return;
    }
}

static void skip_space(void) {
    for (;;) {
        int c = get();
        if (c == EOF) return;
        if (c == ' ' || c == '\t')
            continue;
        if (c == '/') {
            c = get();
            if (c == '*') {
                skip_block_comment();
                continue;
            } else if (c == '/') {
                skip_line();
                continue;
            }
            unget(c);
            unget('/');
            return;
        }
        unget(c);
        return;
    }
}

void skip_cond_incl(void) {
    int nest = 0;
    for (;;) {
        skip_space();
        int c = get();
        if (c == EOF)
            return;
        if (c == '\n')
            continue;
        if (c != '#') {
            skip_line();
            continue;
        }
        skip_space();
        Token *tok = read_cpp_token();
        if (tok->type == TTYPE_NEWLINE)
            continue;
        if (tok->type != TTYPE_IDENT) {
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
        case 'A' ... 'F': r = (r << 4) | (c - 'f' + 10); continue;
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
        if (isalnum(c2) || c2 == '_') {
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
    int c = get();
    if (c == expect)
        return make_punct(t1);
    unget(c);
    return make_punct(els);
}

static Token *read_rep2(char expect1, int t1, char expect2, int t2, char els) {
    int c = get();
    if (c == expect1)
        return make_punct(t1);
    if (c == expect2)
        return make_punct(t2);
    unget(c);
    return make_punct(els);
}

static Token *read_token_int(void) {
    int c = get();
    switch (c) {
    case ' ': case '\t':
        skip_space();
        return space_token;
    case '\n':
        return newline_token;
    case 'L':
        c = get();
        if (c == '"')  return read_string();
        if (c == '\'') return read_char();
        unget(c);
        return read_ident('L');
    case '0' ... '9':
        return read_number(c);
    case 'a' ... 'z': case 'A' ... 'K': case 'M' ... 'Z': case '_':
        return read_ident(c);
    case '/': {
        c = get();
        if (c == '/') {
            skip_line();
            return space_token;
        }
        if (c == '*') {
            skip_block_comment();
            return space_token;
        }
        if (c == '=')
            return make_punct(OP_A_DIV);
        unget(c);
        return make_punct('/');
    }
    case '.': {
        c = get();
        if (c == '.') {
            c = get();
            return make_ident(format("..%c", c));
        }
        unget(c);
        return make_punct('.');
    }
    case '(': case ')': case ',': case ';': case '[': case ']': case '{':
    case '}': case '?': case ':': case '~':
        return make_punct(c);
    case '#': {
        c = get();
        if (c == '#')
            return make_ident("##");
        unget(c);
        return make_punct('#');
    }
    case '+': return read_rep2('+', OP_INC, '=', OP_A_ADD, '+');
    case '-': {
        int c = get();
        if (c == '-')
            return make_punct(OP_DEC);
        if (c == '>')
            return make_punct(OP_ARROW);
        if (c == '=')
            return make_punct(OP_A_SUB);
        unget(c);
        return make_punct('-');
    }
    case '*': return read_rep('=', OP_A_MUL, '*');
    case '%': return read_rep('=', OP_A_MOD, '%');
    case '=': return read_rep('=', OP_EQ, '=');
    case '!': return read_rep('=', OP_NE, '!');
    case '&': return read_rep2('&', OP_LOGAND, '=', OP_A_AND, '&');
    case '|': return read_rep2('|', OP_LOGOR, '=', OP_A_OR, '|');
    case '^': return read_rep('=', OP_A_XOR, '^');
    case '<': {
        c = get();
        if (c == '=')
            return make_punct(OP_LE);
        if (c == '<')
            return read_rep('=', OP_A_LSH, OP_LSH);
        unget(c);
        return make_punct('<');
    }
    case '>': {
        c = get();
        if (c == '=')
            return make_punct(OP_GE);
        if (c == '>')
            return read_rep('=', OP_A_RSH, OP_RSH);
        unget(c);
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

bool read_header_file_name(char **name, bool *std) {
    skip_space();
    char close;
    int c = get();
    if (c == '"') {
        *std = false;
        close = '"';
    } else if (c == '<') {
        *std = true;
        close = '>';
    } else {
        unget(c);
        return false;
    }
    String *s = make_string();
    for (;;) {
        c = get();
        if (c == EOF || c == '\n')
            error("premature end of header name");
        if (c == close)
            break;
        string_append(s, c);
    }
    if (get_cstring(s)[0] == '\0')
        error("header name should not be empty");
    *name = get_cstring(s);
    return true;
}

bool is_punct(Token *tok, int c) {
    return tok && (tok->type == TTYPE_PUNCT) && (tok->punct == c);
}

void set_input_buffer(List *tokens) {
    altbuffer = tokens ? list_reverse(tokens) : NULL;
}

List *get_input_buffer(void) {
    return altbuffer;
}

char *read_error_directive(void) {
    String *s = make_string();
    bool bol = true;
    for (;;) {
        int c = get();
        if (c == EOF) break;
        if (c == '\n') {
            unget(c);
            break;
        }
        if (bol && c == ' ') continue;
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
    while (tok && tok->type == TTYPE_SPACE) {
        tok = read_token_int();
        if (tok) tok->space = true;
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
