#include "8cc.h"

static Dict *macros = &EMPTY_DICT;
static List *buffer = &EMPTY_LIST;
static bool bol = true;

static Token *read_token_int(Dict *hideset);

static Token *read_ident(void) {
    Token *r = read_cpp_token();
    if (r->type != TTYPE_IDENT)
        error("identifier expected, but got %s", token_to_string(r));
    return r;
}

static void expect_newline(void) {
    Token *tok = read_cpp_token();
    if (!tok || tok->type != TTYPE_NEWLINE)
        error("Newline expected, but got %s", token_to_string(tok));
}

static Token *expand(Dict *hideset, Token *tok) {
    if (tok->type != TTYPE_IDENT)
        return tok;
    if (dict_get(hideset, tok->sval))
        return tok;
    List *body = dict_get(macros, tok->sval);
    if (!body)
        return tok;
    dict_put(hideset, tok->sval, (void *)1);
    list_append(buffer, body);
    return read_token_int(hideset);
}

static void read_define(void) {
    Token *name = read_ident();
    List *body = make_list();
    for (;;) {
        Token *tok = read_cpp_token();
        if (!tok || tok->type == TTYPE_NEWLINE)
            break;
        list_push(body, tok);
    }
    dict_put(macros, name->sval, body);
}

static void read_undef(void) {
    Token *name = read_ident();
    expect_newline();
    dict_remove(macros, name->sval);
}

static void read_directive(void) {
    Token *tok = read_cpp_token();
    if (is_ident(tok, "define"))
        read_define();
    else if (is_ident(tok, "undef"))
        read_undef();
    else
        error("unsupported preprocessor directive: %s", token_to_string(tok));
}

void unget_token(Token *tok) {
    list_push(buffer, tok);
}

Token *peek_token(void) {
    Token *r = read_token();
    unget_token(r);
    return r;
}

static Token *read_token_int(Dict *hideset) {
    for (;;) {
        Token *tok = (list_len(buffer) > 0) ? list_pop(buffer) : read_cpp_token();
        if (!tok)
            return NULL;
        if (tok && tok->type == TTYPE_NEWLINE) {
            bol = true;
            continue;
        }
        if (bol && is_punct(tok, '#')) {
            read_directive();
            bol = true;
            continue;
        }
        bol = false;
        return expand(hideset, tok);
    }
}

Token *read_token(void) {
    return read_token_int(&EMPTY_DICT);
}
