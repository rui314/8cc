// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

/*
 * This implements Dave Prosser's C Preprocessing algorithm, described
 * in this document: https://github.com/rui314/8cc/wiki/cpp.algo.pdf
 */

// For fmemopen()
#ifndef linux
#include "compat/fmemopen.c"
#else
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "8cc.h"

bool debug_cpp;
static Map *macros = &EMPTY_MAP;
static Map *imported = &EMPTY_MAP;
static Map *keywords = &EMPTY_MAP;
static Vector *cond_incl_stack = &EMPTY_VECTOR;
static Vector *std_include_path = &EMPTY_VECTOR;
static Token *cpp_token_zero = &(Token){ .kind = TNUMBER, .sval = "0" };
static Token *cpp_token_one = &(Token){ .kind = TNUMBER, .sval = "1" };

typedef void special_macro_handler(Token *tok);
typedef enum { IN_THEN, IN_ELSE } CondInclCtx;
typedef enum { MACRO_OBJ, MACRO_FUNC, MACRO_SPECIAL } MacroType;
typedef struct { CondInclCtx ctx; bool wastrue; } CondIncl;

typedef struct {
    MacroType kind;
    int nargs;
    Vector *body;
    bool is_varg;
    special_macro_handler *fn;
} Macro;

static Macro *make_obj_macro(Vector *body);
static Macro *make_func_macro(Vector *body, int nargs, bool is_varg);
static Macro *make_special_macro(special_macro_handler *fn);
static Token *read_token_sub(bool return_at_eol);
static Token *read_expand(void);

/*
 * Eval
 */

void cpp_eval(char *buf) {
    FILE *fp = fmemopen(buf, strlen(buf), "r");
    set_input_file("(eval)", NULL, fp);
    Vector *toplevels = read_toplevels();
    for (int i = 0; i < vec_len(toplevels); i++)
        emit_toplevel(vec_get(toplevels, i));
}

/*
 * Constructors
 */

static CondIncl *make_cond_incl(CondInclCtx ctx, bool wastrue) {
    CondIncl *r = malloc(sizeof(CondIncl));
    r->ctx = ctx;
    r->wastrue = wastrue;
    return r;
}

static Macro *make_macro(Macro *tmpl) {
    Macro *r = malloc(sizeof(Macro));
    *r = *tmpl;
    return r;
}

static Macro *make_obj_macro(Vector *body) {
    return make_macro(&(Macro){ MACRO_OBJ, .body = body });
}

static Macro *make_func_macro(Vector *body, int nargs, bool is_varg) {
    return make_macro(&(Macro){
            MACRO_FUNC, .nargs = nargs, .body = body, .is_varg = is_varg });
}

static Macro *make_special_macro(special_macro_handler *fn) {
    return make_macro(&(Macro){ MACRO_SPECIAL, .fn = fn });
}

static Token *make_macro_token(int position, bool is_vararg) {
    Token *r = malloc(sizeof(Token));
    r->kind = TMACRO_PARAM;
    r->is_vararg = is_vararg;
    r->hideset = make_map();
    r->position = position;
    r->space = false;
    r->bol = false;
    return r;
}

static Token *copy_token(Token *tok) {
    Token *r = malloc(sizeof(Token));
    *r = *tok;
    return r;
}

static Token *make_number(char *s) {
    Token *r = malloc(sizeof(Token));
    *r = (Token){ TNUMBER, .sval = s };
    return r;
}

static void expect(char id) {
    Token *tok = lex();
    if (!tok || !is_keyword(tok, id))
        error("%c expected, but got %s", t2s(tok));
}

/*
 * Utility functions
 */

bool is_ident(Token *tok, char *s) {
    return tok->kind == TIDENT && !strcmp(tok->sval, s);
}

static bool next(int id) {
    Token *tok = lex();
    if (is_keyword(tok, id))
        return true;
    unget_token(tok);
    return false;
}

static void set_vec_space(Vector *tokens, Token *tmpl) {
    if (vec_len(tokens) == 0)
        return;
    Token *tok = vec_head(tokens);
    tok->space = tmpl->space;
}

/*
 * Macro expander
 */

static Token *read_ident(void) {
    Token *r = lex();
    if (r->kind != TIDENT)
        error("identifier expected, but got %s", t2s(r));
    return r;
}

void expect_newline(void) {
    Token *tok = lex();
    if (!tok || tok->kind != TNEWLINE)
        error("Newline expected, but got %s", t2s(tok));
}

static Vector *do_read_args(Macro *macro) {
    Vector *r = make_vector();
    Vector *arg = make_vector();
    int depth = 0;
    for (;;) {
        Token *tok = lex();
        if (!tok)
            error("unterminated macro argument list");
        if (tok->kind == TNEWLINE)
            continue;
        if (is_keyword(tok, '(')) {
            depth++;
        } else if (depth) {
            if (is_keyword(tok, ')'))
                depth--;
            vec_push(arg, tok);
            continue;
        }
        if (is_keyword(tok, ')')) {
            unget_token(tok);
            vec_push(r, arg);
            return r;
        }
        bool in_threedots = macro->is_varg && vec_len(r) + 1 == macro->nargs;
        if (is_keyword(tok, ',') && !in_threedots) {
            vec_push(r, arg);
            arg = make_vector();
            continue;
        }
        vec_push(arg, tok);
    }
}

static Vector *read_args(Macro *macro) {
    Vector *args = do_read_args(macro);
    if (macro->is_varg) {
        if (vec_len(args) == macro->nargs - 1)
            vec_push(args, make_vector());
        else if (vec_len(args) < macro->nargs)
            error("Macro argument number is less than expected");
        return args;
    }
    if (!macro->is_varg && vec_len(args) != macro->nargs) {
        if (macro->nargs == 0 &&
            vec_len(args) == 1 &&
            vec_len(vec_get(args, 0)) == 0)
            return &EMPTY_VECTOR;
        error("Macro argument number does not match");
    }
    return args;
}

static void map_copy(Map *dst, Map *src) {
    MapIter *i = map_iter(src);
    for (char *k = map_next(i, NULL); k; k = map_next(i, NULL))
        map_put(dst, k, (void *)1);
}

static Map *map_union(Map *a, Map *b) {
    Map *r = make_map();
    map_copy(r, a);
    map_copy(r, b);
    return r;
}

static Map *map_intersection(Map *a, Map *b) {
    Map *r = make_map();
    MapIter *i = map_iter(a);
    for (char *k = map_next(i, NULL); k; k = map_next(i, NULL))
        if (map_get(b, k))
            map_put(r, k, (void *)1);
    return r;
}

static Map *map_append(Map *parent, char *k) {
    Map *r = make_map_parent(parent);
    map_put(r, k, (void *)1);
    return r;
}

static Vector *add_hide_set(Vector *tokens, Map *hideset) {
    Vector *r = make_vector();
    for (int i = 0; i < vec_len(tokens); i++) {
        Token *t = copy_token(vec_get(tokens, i));
        t->hideset = map_union(t->hideset, hideset);
        vec_push(r, t);
    }
    return r;
}

static void paste(Buffer *b, Token *tok) {
    switch (tok->kind) {
    case TIDENT:
    case TNUMBER:
        buf_printf(b, "%s", tok->sval);
        return;
    case TKEYWORD:
        buf_printf(b, "%s", t2s(tok));
        return;
    default:
        error("can't paste: %s", t2s(tok));
    }
}

static Token *glue_tokens(Token *t0, Token *t1) {
    Buffer *b = make_buffer();
    paste(b, t0);
    paste(b, t1);
    Token *r = copy_token(t0);
    r->kind = isdigit(buf_body(b)[0]) ? TNUMBER : TIDENT;
    r->sval = buf_body(b);
    return r;
}

static void glue_push(Vector *tokens, Token *tok) {
    Token *last = vec_pop(tokens);
    vec_push(tokens, glue_tokens(last, tok));
}

static char *join_tokens(Vector *args, bool sep) {
    Buffer *b = make_buffer();
    for (int i = 0; i < vec_len(args); i++) {
        Token *tok = vec_get(args, i);
        if (sep && buf_len(b) && tok->space)
            buf_printf(b, " ");
        switch (tok->kind) {
        case TIDENT:
        case TNUMBER:
            buf_printf(b, "%s", tok->sval);
            break;
        case TKEYWORD:
            buf_printf(b, "%s", t2s(tok));
            break;
        case TCHAR:
            buf_printf(b, "%s", quote_char(tok->c));
            break;
        case TSTRING:
            buf_printf(b, "\"%s\"", quote_cstring(tok->sval));
            break;
        default:
            error("internal error");
        }
    }
    return buf_body(b);
}

static Token *stringize(Token *tmpl, Vector *args) {
    Token *r = copy_token(tmpl);
    r->kind = TSTRING;
    r->sval = join_tokens(args, true);
    return r;
}

static Vector *expand_all(Vector *tokens, Token *tmpl) {
    Vector *r = make_vector();
    Vector *orig = get_input_buffer();
    set_input_buffer(tokens);
    Token *tok;
    while ((tok = read_expand()) != NULL)
        vec_push(r, tok);
    set_vec_space(r, tmpl);
    set_input_buffer(orig);
    return r;
}

static Vector *subst(Macro *macro, Vector *args, Map *hideset) {
    Vector *r = make_vector();
    for (int i = 0; i < vec_len(macro->body); i++) {
        bool islast = (i == vec_len(macro->body) - 1);
        Token *t0 = vec_get(macro->body, i);
        Token *t1 = islast ? NULL : vec_get(macro->body, i + 1);
        bool t0_param = (t0->kind == TMACRO_PARAM);
        bool t1_param = (!islast && t1->kind == TMACRO_PARAM);

        if (is_keyword(t0, '#') && t1_param) {
            vec_push(r, stringize(t0, vec_get(args, t1->position)));
            i++;
            continue;
        }
        if (is_keyword(t0, KSHARPSHARP) && t1_param) {
            Vector *arg = vec_get(args, t1->position);
            if (t1->is_vararg && vec_len(r) > 0 && is_keyword(vec_tail(r), ',')) {
                if (vec_len(arg) > 0)
                    vec_append(r, expand_all(arg, t1));
                else
                    vec_pop(r);
            } else if (vec_len(arg) > 0) {
                glue_push(r, vec_head(arg));
                Vector *tmp = make_vector();
                for (int i = 1; i < vec_len(arg); i++)
                    vec_push(tmp, vec_get(arg, i));
                vec_append(r, expand_all(tmp, t1));
            }
            i++;
            continue;
        }
        if (is_keyword(t0, KSHARPSHARP) && !islast) {
            hideset = t1->hideset;
            glue_push(r, t1);
            i++;
            continue;
        }
        if (t0_param && !islast && is_keyword(t1, KSHARPSHARP)) {
            hideset = t1->hideset;
            Vector *arg = vec_get(args, t0->position);
            if (vec_len(arg) == 0)
                i++;
            else
                vec_append(r, arg);
            continue;
        }
        if (t0_param) {
            Vector *arg = vec_get(args, t0->position);
            vec_append(r, expand_all(arg, t0));
            continue;
        }
        vec_push(r, t0);
    }
    return add_hide_set(r, hideset);
}

static void unget_all(Vector *tokens) {
    for (int i = vec_len(tokens) - 1; i >= 0; i--)
        unget_token(vec_get(tokens, i));
}

static Token *read_expand(void) {
    Token *tok = lex();
    if (!tok) return NULL;
    if (tok->kind == TNEWLINE)
        return read_expand();
    if (tok->kind != TIDENT)
        return tok;
    char *name = tok->sval;
    Macro *macro = map_get(macros, name);
    if (!macro || map_get(tok->hideset, name))
        return tok;

    switch (macro->kind) {
    case MACRO_OBJ: {
        Map *hideset = map_append(tok->hideset, name);
        Vector *tokens = subst(macro, make_vector(), hideset);
        set_vec_space(tokens, tok);
        unget_all(tokens);
        return read_expand();
    }
    case MACRO_FUNC: {
        if (!next('('))
            return tok;
        Vector *args = read_args(macro);
        Token *rparen = lex();
        if (!is_keyword(rparen, ')'))
            error("internal error: %s", t2s(rparen));
        Map *hideset = map_append(map_intersection(tok->hideset, rparen->hideset), name);
        Vector *tokens = subst(macro, args, hideset);
        set_vec_space(tokens, tok);
        unget_all(tokens);
        return read_expand();
    }
    case MACRO_SPECIAL: {
        macro->fn(tok);
        return read_expand();
    }
    default:
        error("internal error");
    }
}

static bool read_funclike_macro_params(Map *param) {
    int pos = 0;
    for (;;) {
        Token *tok = lex();
        if (is_keyword(tok, ')'))
            return false;
        if (pos) {
            if (!is_keyword(tok, ','))
                error("',' expected, but got '%s'", t2s(tok));
            tok = lex();
        }
        if (!tok || tok->kind == TNEWLINE)
            error("missing ')' in macro parameter list");
        if (is_keyword(tok, KTHREEDOTS)) {
            map_put(param, "__VA_ARGS__", make_macro_token(pos++, true));
            expect(')');
            return true;
        }
        if (tok->kind != TIDENT)
            error("identifier expected, but got '%s'", t2s(tok));
        char *arg = tok->sval;
        tok = lex();
        if (is_keyword(tok, KTHREEDOTS)) {
            expect(')');
            map_put(param, arg, make_macro_token(pos++, true));
            return true;
        }
        unget_token(tok);
        map_put(param, arg, make_macro_token(pos++, false));
    }
}

static Vector *read_funclike_macro_body(Map *param) {
    Vector *r = make_vector();
    for (;;) {
        Token *tok = lex();
        if (!tok || tok->kind == TNEWLINE)
            return r;
        if (tok->kind == TIDENT) {
            Token *subst = map_get(param, tok->sval);
            if (subst) {
                subst = copy_token(subst);
                subst->space = tok->space;
                vec_push(r, subst);
                continue;
            }
        }
        vec_push(r, tok);
    }
    return r;
}

static void read_funclike_macro(char *name) {
    Map *param = make_map();
    bool is_varg = read_funclike_macro_params(param);
    Vector *body = read_funclike_macro_body(param);
    Macro *macro = make_func_macro(body, map_size(param), is_varg);
    map_put(macros, name, macro);
}

static void read_obj_macro(char *name) {
    Vector *body = make_vector();
    for (;;) {
        Token *tok = lex();
        if (!tok || tok->kind == TNEWLINE)
            break;
        vec_push(body, tok);
    }
    map_put(macros, name, make_obj_macro(body));
}

/*
 * #define
 */

static void read_define(void) {
    Token *name = read_ident();
    Token *tok = lex();
    if (tok && is_keyword(tok, '(') && !tok->space) {
        read_funclike_macro(name->sval);
        return;
    }
    unget_token(tok);
    read_obj_macro(name->sval);
}

/*
 * #undef
 */

static void read_undef(void) {
    Token *name = read_ident();
    expect_newline();
    map_remove(macros, name->sval);
}

/*
 * defined()
 */

static Token *read_defined_op(void) {
    Token *tok = lex();
    if (is_keyword(tok, '(')) {
        tok = lex();
        expect(')');
    }
    if (tok->kind != TIDENT)
        error("Identifier expected, but got %s", t2s(tok));
    return map_get(macros, tok->sval) ? cpp_token_one : cpp_token_zero;
}

/*
 * #if and the like
 */

static Vector *read_intexpr_line(void) {
    Vector *r = make_vector();
    for (;;) {
        Token *tok = read_token_sub(true);
        if (!tok) return r;
        if (is_ident(tok, "defined"))
            vec_push(r, read_defined_op());
        else if (tok->kind == TIDENT)
            vec_push(r, cpp_token_one);
        else
            vec_push(r, tok);
    }
}

static bool read_constexpr(void) {
    Vector *orig = get_input_buffer();
    set_input_buffer(read_intexpr_line());
    Node *expr = read_expr();
    Vector *buf = get_input_buffer();
    if (vec_len(buf) > 0)
        error("Stray token: %s", t2s(vec_get(buf, 0)));
    set_input_buffer(orig);
    return eval_intexpr(expr);
}

static void read_if_generic(bool cond) {
    vec_push(cond_incl_stack, make_cond_incl(IN_THEN, cond));
    if (!cond)
        skip_cond_incl();
}

static void read_if(void) {
    read_if_generic(read_constexpr());
}

static void read_ifdef_generic(bool is_ifdef) {
    Token *tok = lex();
    if (!tok || tok->kind != TIDENT)
        error("identifier expected, but got %s", t2s(tok));
    bool cond = map_get(macros, tok->sval);
    expect_newline();
    read_if_generic(is_ifdef ? cond : !cond);
}

static void read_ifdef(void) {
    read_ifdef_generic(true);
}

static void read_ifndef(void) {
    read_ifdef_generic(false);
}

static void read_else(void) {
    if (vec_len(cond_incl_stack) == 0)
        error("stray #else");
    CondIncl *ci = vec_tail(cond_incl_stack);
    if (ci->ctx == IN_ELSE)
        error("#else appears in #else");
    expect_newline();
    if (ci->wastrue)
        skip_cond_incl();
}

static void read_elif(void) {
    if (vec_len(cond_incl_stack) == 0)
        error("stray #elif");
    CondIncl *ci = vec_tail(cond_incl_stack);
    if (ci->ctx == IN_ELSE)
        error("#elif after #else");
    if (ci->wastrue)
        skip_cond_incl();
    else if (read_constexpr())
        ci->wastrue = true;
    else
        skip_cond_incl();
}

static void read_endif(void) {
    if (vec_len(cond_incl_stack) == 0)
        error("stray #endif");
    vec_pop(cond_incl_stack);
    expect_newline();
}

/*
 * #error and #warning
 */

static void read_error(void) {
    error("#error: %s", read_error_directive());
}

static void read_warning(void) {
    warn("#warning: %s", read_error_directive());
}

/*
 * #include
 */

static char *read_cpp_header_name(bool *std) {
    if (!get_input_buffer()) {
        char *r = read_header_file_name(std);
        if (r)
            return r;
    }
    Token *tok = read_expand();
    if (!tok || tok->kind == TNEWLINE)
        error("expected file name, but got %s", t2s(tok));
    if (tok->kind == TSTRING) {
        *std = false;
        return tok->sval;
    }
    if (!is_keyword(tok, '<'))
        error("'<' expected, but got %s", t2s(tok));
    Vector *tokens = make_vector();
    for (;;) {
        Token *tok = read_expand();
        if (!tok || tok->kind == TNEWLINE)
            error("premature end of header name");
        if (is_keyword(tok, '>'))
            break;
        vec_push(tokens, tok);
    }
    *std = true;
    return join_tokens(tokens, false);
}

static bool try_include(char *dir, char *filename, bool isimport) {
    char *path = format("%s/%s", dir, filename);
    if (isimport && map_get(imported, path))
        return true;
    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;
    if (isimport)
        map_put(imported, path, (void *)1);
    push_input_file(path, path, fp);
    return true;
}

static void read_include(bool isimport) {
    bool std;
    char *filename = read_cpp_header_name(&std);
    expect_newline();
    if (!std) {
        char *cur = get_current_file();
        char *dir = cur ? dirname(strdup(cur)) : ".";
        if (try_include(dir, filename, isimport))
            return;
    }
    for (int i = vec_len(std_include_path) - 1; i >= 0; i--)
        if (try_include(vec_get(std_include_path, i), filename, isimport))
            return;
    error("Cannot find header file: %s", filename);
}

/*
 * #pragma
 */

static void parse_pragma_operand(char *s) {
    if (!strcmp(s, "enable_warning"))
        enable_warning = true;
    else if (!strcmp(s, "disable_warning"))
        enable_warning = false;
    else
        error("Unknown #pragma: %s", s);
}

static void read_pragma(void) {
    Token *tok = read_ident();
    parse_pragma_operand(tok->sval);
}

/*
 * #line
 */

static bool is_digit_sequence(char *p) {
    for (; *p; p++)
        if (!isdigit(*p))
            return false;
    return true;
}

static void read_line(void) {
    Token *tok = lex();
    if (!tok || tok->kind != TNUMBER || !is_digit_sequence(tok->sval))
        error("number expected after #line, but got %s", t2s(tok));
    int line = atoi(tok->sval);
    tok = lex();
    char *filename = NULL;
    if (tok && tok->kind == TSTRING) {
        filename = tok->sval;
        expect_newline();
    } else if (tok->kind != TNEWLINE) {
        error("newline or a source name are expected, but got %s", t2s(tok));
    }
    set_current_line(line);
    if (filename)
        set_current_displayname(filename);
}

/*
 * #-directive
 */

static void read_directive(void) {
    Token *tok = lex();
    if (is_ident(tok, "define"))       read_define();
    else if (is_ident(tok, "undef"))   read_undef();
    else if (is_ident(tok, "if"))      read_if();
    else if (is_ident(tok, "ifdef"))   read_ifdef();
    else if (is_ident(tok, "ifndef"))  read_ifndef();
    else if (is_ident(tok, "else"))    read_else();
    else if (is_ident(tok, "elif"))    read_elif();
    else if (is_ident(tok, "endif"))   read_endif();
    else if (is_ident(tok, "error"))   read_error();
    else if (is_ident(tok, "warning")) read_warning();
    else if (is_ident(tok, "include")) read_include(false);
    else if (is_ident(tok, "import"))  read_include(true);
    else if (is_ident(tok, "pragma"))  read_pragma();
    else if (is_ident(tok, "line"))    read_line();
    else if (tok->kind != TNEWLINE)
        error("unsupported preprocessor directive: %s", t2s(tok));
}

/*
 * Special macros
 */

static struct tm *gettime(void) {
    static struct tm tm;
    static bool init = false;
    if (init)
        return &tm;
    init = true;
    time_t timet = time(NULL);
    localtime_r(&timet, &tm);
    return &tm;
}

static void make_token_pushback(Token *tmpl, int kind, char *sval) {
    Token *tok = copy_token(tmpl);
    tok->kind = kind;
    tok->sval = sval;
    unget_token(tok);
}

static void handle_date_macro(Token *tmpl) {
    struct tm *now = gettime();
    char *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char *sval = format("%s % 2d %04d", month[now->tm_mon], now->tm_mday, 1900 + now->tm_year);
    make_token_pushback(tmpl, TSTRING, sval);
}

static void handle_time_macro(Token *tmpl) {
    struct tm *now = gettime();
    char *sval = format("%02d:%02d:%02d", now->tm_hour, now->tm_min, now->tm_sec);
    make_token_pushback(tmpl, TSTRING, sval);
}

static void handle_file_macro(Token *tmpl) {
    make_token_pushback(tmpl, TSTRING, get_current_displayname());
}

static void handle_line_macro(Token *tmpl) {
    make_token_pushback(tmpl, TNUMBER, format("%d", get_current_line()));
}

static void handle_pragma_macro(Token *tmpl) {
    expect('(');
    Token *operand = read_token();
    if (!operand || operand->kind != TSTRING)
        error("_Pragma takes a string literal, but got %s", t2s(operand));
    expect(')');
    parse_pragma_operand(operand->sval);
    make_token_pushback(tmpl, TNUMBER, "1");
}

static void handle_counter_macro(Token *tmpl) {
    static int counter = 0;
    make_token_pushback(tmpl, TNUMBER, format("%d", counter++));
}

/*
 * Initializer
 */

static char *drop_last_slash(char *s) {
    char *r = format("%s", s);
    char *p = r + strlen(r) - 1;
    if (*p == '/')
        *p = '\0';
    return r;
}

void add_include_path(char *path) {
    vec_push(std_include_path, drop_last_slash(path));
}

/*
 * Initializer
 */

static void define_obj_macro(char *name, Token *value) {
    map_put(macros, name, make_obj_macro(make_vector1(value)));
}

static void define_special_macro(char *name, special_macro_handler *fn) {
    map_put(macros, name, make_special_macro(fn));
}

static void init_keywords(void) {
#define op(id, str)         map_put(keywords, str, (void *)id);
#define keyword(id, str, _) map_put(keywords, str, (void *)id);
#include "keyword.h"
#undef keyword
#undef id
}

static void init_predefined_macros(void) {
    vec_push(std_include_path, "/usr/include/x86_64-linux-gnu");
    vec_push(std_include_path, "/usr/include/linux");
    vec_push(std_include_path, "/usr/include");
    vec_push(std_include_path, "/usr/local/include");
    vec_push(std_include_path, "/usr/local/lib/8cc/include");
    vec_push(std_include_path, BUILD_DIR "/include");

    define_special_macro("__DATE__", handle_date_macro);
    define_special_macro("__TIME__", handle_time_macro);
    define_special_macro("__FILE__", handle_file_macro);
    define_special_macro("__LINE__", handle_line_macro);
    define_special_macro("_Pragma",  handle_pragma_macro);
    define_special_macro("__COUNTER__", handle_counter_macro);

    char *predefined[] = {
        "__8cc__", "__amd64", "__amd64__", "__x86_64", "__x86_64__",
        "linux", "__linux", "__linux__", "__gnu_linux__", "__unix", "__unix__",
        "_LP64", "__LP64__", "__ELF__", "__STDC__", "__STDC_HOSTED__",
        "__STDC_NO_ATOMICS__", "__STDC_NO_COMPLEX__", "__STDC_NO_THREADS__",
        "__STDC_NO_VLA__", };

    for (int i = 0; i < sizeof(predefined) / sizeof(*predefined); i++)
        define_obj_macro(predefined[i], cpp_token_one);

    define_obj_macro("__STDC_VERSION__", make_number("201112L"));
    define_obj_macro("__SIZEOF_SHORT__", make_number("2"));
    define_obj_macro("__SIZEOF_INT__", make_number("4"));
    define_obj_macro("__SIZEOF_LONG__", make_number("8"));
    define_obj_macro("__SIZEOF_LONG_LONG__", make_number("8"));
    define_obj_macro("__SIZEOF_FLOAT__", make_number("4"));
    define_obj_macro("__SIZEOF_DOUBLE__", make_number("8"));
    define_obj_macro("__SIZEOF_LONG_DOUBLE__", make_number("8"));
    define_obj_macro("__SIZEOF_POINTER__", make_number("8"));
    define_obj_macro("__SIZEOF_PTRDIFF_T__", make_number("8"));
    define_obj_macro("__SIZEOF_SIZE_T__", make_number("8"));
}

void cpp_init(void) {
    init_keywords();
    init_predefined_macros();
}

/*
 * Keyword
 */

static Token *maybe_convert_keyword(Token *tok) {
    if (!tok)
        return NULL;
    if (tok->kind != TIDENT)
        return tok;
    int id = (intptr_t)map_get(keywords, tok->sval);
    if (!id)
        return tok;
    Token *r = copy_token(tok);
    r->kind = TKEYWORD;
    r->id = id;
    return r;
}

/*
 * Public intefaces
 */

void unget_token(Token *tok) {
    unget_cpp_token(tok);
}

Token *peek_token(void) {
    Token *r = read_token();
    unget_token(r);
    return r;
}

static Token *read_token_sub(bool return_at_eol) {
    for (;;) {
        Token *tok = lex();
        if (!tok)
            return NULL;
        if (tok && tok->kind == TNEWLINE) {
            if (return_at_eol)
                return NULL;
            continue;
        }
        if (tok->bol && is_keyword(tok, '#')) {
            read_directive();
            continue;
        }
        unget_token(tok);
        Token *r = read_expand();
        if (r && r->bol && is_keyword(r, '#') && map_size(r->hideset) == 0) {
            read_directive();
            continue;
        }
        return r;
    }
}

Token *read_token(void) {
    Token *tok;
    for (;;) {
        tok = read_token_sub(false);
        if (!tok) return NULL;
        assert(tok->kind != TNEWLINE);
        assert(tok->kind != TSPACE);
        assert(tok->kind != TMACRO_PARAM);
        if (tok->kind != TSTRING)
            break;
        Token *tok2 = read_token_sub(false);
        if (!tok2 || tok2->kind != TSTRING) {
            unget_token(tok2);
            break;
        }
        Token *conc = copy_token(tok);
        conc->sval = format("%s%s", tok->sval, tok2->sval);
        unget_token(conc);
    }
    Token *r = maybe_convert_keyword(tok);
    if (debug_cpp)
        fprintf(stderr, "  token=%s\n", t2s(r));
    return r;
}
