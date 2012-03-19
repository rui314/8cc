#include <stdlib.h>
#include <ctype.h>
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
    r->column = 1;
    r->fp = fp;
    return r;
}

static __attribute__((constructor)) void init(void) {
    file = make_file("(stdin)", stdin);
}

static Token *make_token(int type) {
    Token *r = malloc(sizeof(Token));
    r->type = type;
    r->hideset = make_dict(NULL);
    r->space = false;
    r->bol = false;
    r->file = file->name;
    r->line = file->line;
    r->column = file->column;
    return r;
}

static Token *make_ident(char *p) {
    Token *r = make_token(TTYPE_IDENT);
    r->sval = p;
    return r;
}

static Token *make_strtok(char *s) {
    Token *r = make_token(TTYPE_STRING);
    r->sval = s;
    return r;
}

static Token *make_punct(int punct) {
    Token *r = make_token(TTYPE_PUNCT);
    r->punct = punct;
    return r;
}

static Token *make_number(char *s) {
    Token *r = make_token(TTYPE_NUMBER);
    r->sval = s;
    return r;
}

static Token *make_char(char c) {
    Token *r = make_token(TTYPE_CHAR);
    r->c = c;
    return r;
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

static Token *read_char(void) {
    char c = get();
    if (c == EOF) goto err;
    if (c == '\\') {
        c = get();
        if (c == EOF) goto err;
    }
    char c2 = get();
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
        int c = get();
        if (c == EOF)
            error("Unterminated string");
        if (c == '"')
            break;
        if (c == '\\') {
            c = get();
            switch (c) {
            case EOF: error("Unterminated \\");
            case '\"': break;
            case '\\': c = '\\'; break;
            case 'n': c = '\n'; break;
            default: error("Unknown quote: %c", c);
            }
        }
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

static Token *read_rep(char expect, int t1, int t2) {
    int c = get();
    if (c == expect)
        return make_punct(t1);
    unget(c);
    return make_punct(t2);
}

static Token *read_token_int(void) {
    int c = get();
    switch (c) {
    case ' ': case '\t':
        skip_space();
        return space_token;
    case '\n':
        return newline_token;
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
        c = get();
        if (c == '/') {
            skip_line();
            return read_token_int();
        }
        if (c == '*') {
            skip_block_comment();
            return read_token_int();
        }
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
    case '*': case '(': case ')': case ',': case ';': case '[': case ']':
    case '{': case '}': case '?': case ':':
        return make_punct(c);
    case '#': {
        c = get();
        if (c == '#')
            return make_ident("##");
        unget(c);
        return make_punct('#');
    }
    case '-':
        c = get();
        if (c == '-') return make_punct(OP_DEC);
        if (c == '>') return make_punct(OP_ARROW);
        unget(c);
        return make_punct('-');
    case '<': return read_rep('=', OP_LE, '<');
    case '>': return read_rep('=', OP_GE, '>');
    case '=': return read_rep('=', OP_EQ, '=');
    case '!': return read_rep('=', OP_NE, '!');
    case '+': return read_rep('+', OP_INC, '+');
    case '&': return read_rep('&', OP_LOGAND, '&');
    case '|': return read_rep('|', OP_LOGOR, '|');
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
