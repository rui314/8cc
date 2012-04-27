// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

/*
 * This implements Dave Prosser's C Preprocessing algorithm, described
 * in this document: https://github.com/rui314/8cc/wiki/cpp.algo.pdf
 */

// For fmemopen()
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "8cc.h"

bool debug_cpp;
static Dict *macros = &EMPTY_DICT;
static List *cond_incl_stack = &EMPTY_LIST;
static List *std_include_path = &EMPTY_LIST;
static Token *cpp_token_zero = &(Token){ .type = TNUMBER, .sval = "0" };
static Token *cpp_token_one = &(Token){ .type = TNUMBER, .sval = "1" };
static struct tm *current_time;
static int macro_counter;

typedef void special_macro_handler(Token *tok);
typedef enum { IN_THEN, IN_ELSE } CondInclCtx;
typedef enum { MACRO_OBJ, MACRO_FUNC, MACRO_SPECIAL } MacroType;
typedef struct { CondInclCtx ctx; bool wastrue; } CondIncl;

typedef struct {
    MacroType type;
    int nargs;
    List *body;
    bool is_varg;
    special_macro_handler *fn;
} Macro;

static Macro *make_obj_macro(List *body);
static Macro *make_func_macro(List *body, int nargs, bool is_varg);
static Macro *make_special_macro(special_macro_handler *fn);
static Token *read_token_sub(bool return_at_eol);
static Token *read_expand(void);

/*----------------------------------------------------------------------
 * Eval
 */

void cpp_eval(char *buf) {
    FILE *fp = fmemopen(buf, strlen(buf), "r");
    set_input_file("(eval)", NULL, fp);
    List *toplevels = read_toplevels();
    for (Iter *i = list_iter(toplevels); !iter_end(i);)
        emit_toplevel(iter_next(i));
}

/*----------------------------------------------------------------------
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

static Macro *make_obj_macro(List *body) {
    return make_macro(&(Macro){ MACRO_OBJ, .body = body });
}

static Macro *make_func_macro(List *body, int nargs, bool is_varg) {
    return make_macro(&(Macro){
            MACRO_FUNC, .nargs = nargs, .body = body, .is_varg = is_varg });
}

static Macro *make_special_macro(special_macro_handler *fn) {
    return make_macro(&(Macro){ MACRO_SPECIAL, .fn = fn });
}

static Token *make_macro_token(int position, bool is_vararg) {
    Token *r = malloc(sizeof(Token));
    r->type = TMACRO_PARAM;
    r->is_vararg = is_vararg;
    r->hideset = make_dict(NULL);
    r->position = position;
    r->nspace = 0;
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

static void expect(char punct) {
    Token *tok = read_cpp_token();
    if (!tok || !is_punct(tok, punct))
        error("%c expected, but got %s", t2s(tok));
}

/*----------------------------------------------------------------------
 * Utility functions
 */

bool is_ident(Token *tok, char *s) {
    return tok->type == TIDENT && !strcmp(tok->sval, s);
}

static bool next(int punct) {
    Token *tok = read_cpp_token();
    if (is_punct(tok, punct))
        return true;
    unget_token(tok);
    return false;
}

static void set_list_nspace(List *tokens, Token *tmpl) {
    if (list_len(tokens) == 0)
        return;
    Token *tok = list_head(tokens);
    tok->nspace = tmpl->nspace;
}

/*----------------------------------------------------------------------
 * Macro expander
 */

static Token *read_ident(void) {
    Token *r = read_cpp_token();
    if (r->type != TIDENT)
        error("identifier expected, but got %s", t2s(r));
    return r;
}

void expect_newline(void) {
    Token *tok = read_cpp_token();
    if (!tok || tok->type != TNEWLINE)
        error("Newline expected, but got %s", t2s(tok));
}

static List *read_args_int(Macro *macro) {
    List *r = make_list();
    List *arg = make_list();
    int depth = 0;
    for (;;) {
        Token *tok = read_cpp_token();
        if (!tok)
            error("unterminated macro argument list");
        if (tok->type == TNEWLINE)
            continue;
        if (is_punct(tok, '(')) {
            depth++;
        } else if (depth) {
            if (is_punct(tok, ')'))
                depth--;
            list_push(arg, tok);
            continue;
        }
        if (is_punct(tok, ')')) {
            unget_token(tok);
            list_push(r, arg);
            return r;
        }
        bool in_threedots = macro->is_varg && list_len(r) + 1 == macro->nargs;
        if (is_punct(tok, ',') && !in_threedots) {
            list_push(r, arg);
            arg = make_list();
            continue;
        }
        list_push(arg, tok);
    }
}

static List *read_args(Macro *macro) {
    List *args = read_args_int(macro);
    if (macro->is_varg) {
        if (list_len(args) == macro->nargs - 1)
            list_push(args, make_list());
        else if (list_len(args) < macro->nargs)
            error("Macro argument number is less than expected");
        return args;
    }
    if (!macro->is_varg && list_len(args) != macro->nargs) {
        if (macro->nargs == 0 &&
            list_len(args) == 1 &&
            list_len(list_get(args, 0)) == 0)
            return &EMPTY_LIST;
        error("Macro argument number does not match");
    }
    return args;
}

static Dict *dict_union(Dict *a, Dict *b) {
    Dict *r = make_dict(NULL);
    for (Iter *i = list_iter(dict_keys(a)); !iter_end(i);) {
        char *key = iter_next(i);
        dict_put(r, key, dict_get(a, key));
    }
    for (Iter *i = list_iter(dict_keys(b)); !iter_end(i);) {
        char *key = iter_next(i);
        dict_put(r, key, dict_get(b, key));
    }
    return r;
}

static Dict *dict_intersection(Dict *a, Dict *b) {
    Dict *r = make_dict(NULL);
    for (Iter *i = list_iter(dict_keys(a)); !iter_end(i);) {
        char *key = iter_next(i);
        if (dict_get(b, key))
            dict_put(r, key, (void *)1);
    }
    return r;
}

static Dict *dict_append(Dict *dict, char *s) {
    Dict *r = make_dict(dict);
    dict_put(r, s, (void *)1);
    return r;
}

static List *add_hide_set(List *tokens, Dict *hideset) {
    List *r = make_list();
    for (Iter *i = list_iter(tokens); !iter_end(i);) {
        Token *t = copy_token(iter_next(i));
        t->hideset = dict_union(t->hideset, hideset);
        list_push(r, t);
    }
    return r;
}

static void paste(String *s, Token *tok) {
    switch (tok->type) {
    case TIDENT:
    case TNUMBER:
        string_appendf(s, "%s", tok->sval);
        return;
    case TPUNCT:
        string_appendf(s, "%s", t2s(tok));
        return;
    default:
        error("can't paste: %s", t2s(tok));
    }
}

static Token *glue_tokens(Token *t0, Token *t1) {
    String *s = make_string();
    paste(s, t0);
    paste(s, t1);
    Token *r = copy_token(t0);
    r->type = isdigit(get_cstring(s)[0]) ? TNUMBER : TIDENT;
    r->sval = get_cstring(s);
    return r;
}

static void glue_push(List *tokens, Token *tok) {
    assert(list_len(tokens) > 0);
    Token *last = list_pop(tokens);
    list_push(tokens, glue_tokens(last, tok));
}

static char *join_tokens(List *args, bool sep) {
    String *s = make_string();
    for (Iter *i = list_iter(args); !iter_end(i);) {
        Token *tok = iter_next(i);
        if (sep && string_len(s) && tok->nspace)
            string_appendf(s, " ");
        switch (tok->type) {
        case TIDENT:
        case TNUMBER:
            string_appendf(s, "%s", tok->sval);
            break;
        case TPUNCT:
            string_appendf(s, "%s", t2s(tok));
            break;
        case TCHAR:
            string_appendf(s, "%s", quote_char(tok->c));
            break;
        case TSTRING:
            string_appendf(s, "\"%s\"", quote_cstring(tok->sval));
            break;
        default:
            error("internal error");
        }
    }
    return get_cstring(s);
}

static Token *stringize(Token *tmpl, List *args) {
    Token *r = copy_token(tmpl);
    r->type = TSTRING;
    r->sval = join_tokens(args, true);
    return r;
}

static List *expand_all(List *tokens, Token *tmpl) {
    List *r = make_list();
    List *orig = get_input_buffer();
    set_input_buffer(tokens);
    Token *tok;
    while ((tok = read_expand()) != NULL)
        list_push(r, tok);
    set_list_nspace(r, tmpl);
    set_input_buffer(orig);
    return r;
}

static List *subst(Macro *macro, List *args, Dict *hideset) {
    List *r = make_list();
    for (int i = 0; i < list_len(macro->body); i++) {
        bool islast = (i == list_len(macro->body) - 1);
        Token *t0 = list_get(macro->body, i);
        Token *t1 = islast ? NULL : list_get(macro->body, i + 1);
        bool t0_param = (t0->type == TMACRO_PARAM);
        bool t1_param = (!islast && t1->type == TMACRO_PARAM);

        if (is_punct(t0, '#') && t1_param) {
            list_push(r, stringize(t0, list_get(args, t1->position)));
            i++;
            continue;
        }
        if (is_ident(t0, "##") && t1_param) {
            List *arg = list_get(args, t1->position);
            if (t1->is_vararg && list_len(r) > 0 && is_punct(list_tail(r), ',')) {
                if (list_len(arg) > 0)
                    list_append(r, expand_all(arg, t1));
                else
                    list_pop(r);
            } else if (list_len(arg) > 0) {
                glue_push(r, list_head(arg));
                List *tmp = list_copy(arg);
                list_shift(tmp);
                list_append(r, expand_all(tmp, t1));
            }
            i++;
            continue;
        }
        if (is_ident(t0, "##") && !islast) {
            hideset = t1->hideset;
            glue_push(r, t1);
            i++;
            continue;
        }
        if (t0_param && !islast && is_ident(t1, "##")) {
            hideset = t1->hideset;
            List *arg = list_get(args, t0->position);
            if (list_len(arg) == 0)
                i++;
            else
                list_append(r, arg);
            continue;
        }
        if (t0_param) {
            List *arg = list_get(args, t0->position);
            list_append(r, expand_all(arg, t0));
            continue;
        }
        list_push(r, t0);
    }
    return add_hide_set(r, hideset);
}

static void unget_all(List *tokens) {
    for (Iter *i = list_iter(list_reverse(tokens)); !iter_end(i);)
        unget_token(iter_next(i));
}

static Token *read_expand(void) {
    Token *tok = read_cpp_token();
    if (!tok) return NULL;
    if (tok->type == TNEWLINE)
        return read_expand();
    if (tok->type != TIDENT)
        return tok;
    char *name = tok->sval;
    Macro *macro = dict_get(macros, name);
    if (!macro || dict_get(tok->hideset, name))
        return tok;

    switch (macro->type) {
    case MACRO_OBJ: {
        Dict *hideset = dict_append(tok->hideset, name);
        List *tokens = subst(macro, make_list(), hideset);
        set_list_nspace(tokens, tok);
        unget_all(tokens);
        return read_expand();
    }
    case MACRO_FUNC: {
        if (!next('('))
            return tok;
        List *args = read_args(macro);
        Token *rparen = read_cpp_token();
        if (!is_punct(rparen, ')'))
            error("internal error: %s", t2s(rparen));
        Dict *hideset = dict_append(dict_intersection(tok->hideset, rparen->hideset), name);
        List *tokens = subst(macro, args, hideset);
        set_list_nspace(tokens, tok);
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

static bool read_funclike_macro_params(Dict *param) {
    int pos = 0;
    for (;;) {
        Token *tok = read_cpp_token();
        if (is_punct(tok, ')'))
            return false;
        if (pos) {
            if (!is_punct(tok, ','))
                error("',' expected, but got '%s'", t2s(tok));
            tok = read_cpp_token();
        }
        if (!tok || tok->type == TNEWLINE)
            error("missing ')' in macro parameter list");
        if (is_ident(tok, "...")) {
            dict_put(param, "__VA_ARGS__", make_macro_token(pos++, true));
            expect(')');
            return true;
        }
        if (tok->type != TIDENT)
            error("identifier expected, but got '%s'", t2s(tok));
        char *arg = tok->sval;
        tok = read_cpp_token();
        if (is_ident(tok, "...")) {
            expect(')');
            dict_put(param, arg, make_macro_token(pos++, true));
            return true;
        }
        unget_token(tok);
        dict_put(param, arg, make_macro_token(pos++, false));
    }
}

static List *read_funclike_macro_body(Dict *param) {
    List *r = make_list();
    for (;;) {
        Token *tok = read_cpp_token();
        if (!tok || tok->type == TNEWLINE)
            return r;
        if (tok->type == TIDENT) {
            Token *subst = dict_get(param, tok->sval);
            if (subst) {
                subst = copy_token(subst);
                subst->nspace = tok->nspace;
                list_push(r, subst);
                continue;
            }
        }
        list_push(r, tok);
    }
    return r;
}

static void read_funclike_macro(char *name) {
    Dict *param = make_dict(NULL);
    bool is_varg = read_funclike_macro_params(param);
    List *body = read_funclike_macro_body(param);
    Macro *macro = make_func_macro(body, list_len(dict_keys(param)), is_varg);
    dict_put(macros, name, macro);
}

static void read_obj_macro(char *name) {
    List *body = make_list();
    for (;;) {
        Token *tok = read_cpp_token();
        if (!tok || tok->type == TNEWLINE)
            break;
        list_push(body, tok);
    }
    dict_put(macros, name, make_obj_macro(body));
}

/*----------------------------------------------------------------------
 * #define
 */

static void read_define(void) {
    Token *name = read_ident();
    Token *tok = read_cpp_token();
    if (tok && is_punct(tok, '(') && !tok->nspace) {
        read_funclike_macro(name->sval);
        return;
    }
    unget_token(tok);
    read_obj_macro(name->sval);
}

/*----------------------------------------------------------------------
 * #undef
 */

static void read_undef(void) {
    Token *name = read_ident();
    expect_newline();
    dict_remove(macros, name->sval);
}

/*----------------------------------------------------------------------
 * defined()
 */

static Token *read_defined_op(void) {
    Token *tok = read_cpp_token();
    if (is_punct(tok, '(')) {
        tok = read_cpp_token();
        expect(')');
    }
    if (tok->type != TIDENT)
        error("Identifier expected, but got %s", t2s(tok));
    return dict_get(macros, tok->sval) ?
        cpp_token_one : cpp_token_zero;
}

/*----------------------------------------------------------------------
 * #if and the like
 */

static List *read_intexpr_line(void) {
    List *r = make_list();
    for (;;) {
        Token *tok = read_token_sub(true);
        if (!tok) return r;
        if (is_ident(tok, "defined"))
            list_push(r, read_defined_op());
        else if (tok->type == TIDENT)
            list_push(r, cpp_token_one);
        else
            list_push(r, tok);
    }
}

static bool read_constexpr(void) {
    List *orig = get_input_buffer();
    set_input_buffer(read_intexpr_line());
    Node *expr = read_expr();
    List *buf = get_input_buffer();
    if (list_len(buf) > 0)
        error("Stray token: %s", t2s(list_shift(buf)));
    set_input_buffer(orig);
    return eval_intexpr(expr);
}

static void read_if_generic(bool cond) {
    list_push(cond_incl_stack, make_cond_incl(IN_THEN, cond));
    if (!cond)
        skip_cond_incl();
}

static void read_if(void) {
    read_if_generic(read_constexpr());
}

static void read_ifdef_generic(bool is_ifdef) {
    Token *tok = read_cpp_token();
    if (!tok || tok->type != TIDENT)
        error("identifier expected, but got %s", t2s(tok));
    bool cond = dict_get(macros, tok->sval);
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
    if (list_len(cond_incl_stack) == 0)
        error("stray #else");
    CondIncl *ci = list_tail(cond_incl_stack);
    if (ci->ctx == IN_ELSE)
        error("#else appears in #else");
    expect_newline();
    if (ci->wastrue)
        skip_cond_incl();
}

static void read_elif(void) {
    if (list_len(cond_incl_stack) == 0)
        error("stray #elif");
    CondIncl *ci = list_tail(cond_incl_stack);
    if (ci->ctx == IN_ELSE)
        error("#elif after #else");
    if (ci->wastrue)
        skip_cond_incl();
    else if (read_constexpr())
        ci->wastrue = true;
}

static void read_endif(void) {
    if (list_len(cond_incl_stack) == 0)
        error("stray #endif");
    list_pop(cond_incl_stack);
    expect_newline();
}

/*----------------------------------------------------------------------
 * #error
 */

static void read_error(void) {
    error("#error: %s", read_error_directive());
}

/*----------------------------------------------------------------------
 * #include
 */

static char *read_cpp_header_name(bool *std) {
    if (!get_input_buffer()) {
        char *r = read_header_file_name(std);
        if (r)
            return r;
    }
    Token *tok = read_expand();
    if (!tok || tok->type == TNEWLINE)
        error("expected file name, but got %s", t2s(tok));
    if (tok->type == TSTRING) {
        *std = false;
        return tok->sval;
    }
    if (!is_punct(tok, '<'))
        error("'<' expected, but got %s", t2s(tok));
    List *tokens = make_list();
    for (;;) {
        Token *tok = read_expand();
        if (!tok || tok->type == TNEWLINE)
            error("premature end of header name");
        if (is_punct(tok, '>'))
            break;
        list_push(tokens, tok);
    }
    *std = true;
    return join_tokens(tokens, false);
}

static bool try_include(char *dir, char *filename) {
    char *path = format("%s/%s", dir, filename);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;
    push_input_file(path, path, fp);
    return true;
}

static void read_include(void) {
    bool std;
    char *filename = read_cpp_header_name(&std);
    expect_newline();
    if (!std) {
        if (get_current_file()) {
            char *buf = format("%s", get_current_file());
            if (try_include(dirname(buf), filename))
                return;
        } else if (try_include(".", filename)) {
            return;
        }
    }
    for (Iter *i = list_iter(std_include_path); !iter_end(i);) {
        if (try_include(iter_next(i), filename))
            return;
    }
    error("Cannot find header file: %s", filename);
}

/*----------------------------------------------------------------------
 * #line
 */

static bool is_digit_sequence(char *p) {
    for (; *p; p++)
        if (!isdigit(*p))
            return false;
    return true;
}

static void read_line(void) {
    Token *tok = read_cpp_token();
    if (!tok || tok->type != TNUMBER || !is_digit_sequence(tok->sval))
        error("number expected after #line, but got %s", t2s(tok));
    int line = atoi(tok->sval);
    tok = read_cpp_token();
    char *filename = NULL;
    if (tok && tok->type == TSTRING) {
        filename = tok->sval;
        expect_newline();
    } else if (tok->type != TNEWLINE) {
        error("newline or a source name are expected, but got %s", t2s(tok));
    }
    set_current_line(line);
    if (filename)
        set_current_displayname(filename);
}

/*----------------------------------------------------------------------
 * #-directive
 */

static void read_directive(void) {
    Token *tok = read_cpp_token();
    if (is_ident(tok, "define"))       read_define();
    else if (is_ident(tok, "undef"))   read_undef();
    else if (is_ident(tok, "if"))      read_if();
    else if (is_ident(tok, "ifdef"))   read_ifdef();
    else if (is_ident(tok, "ifndef"))  read_ifndef();
    else if (is_ident(tok, "else"))    read_else();
    else if (is_ident(tok, "elif"))    read_elif();
    else if (is_ident(tok, "endif"))   read_endif();
    else if (is_ident(tok, "error"))   read_error();
    else if (is_ident(tok, "include")) read_include();
    else if (is_ident(tok, "line"))    read_line();
    else if (tok->type != TNEWLINE)
        error("unsupported preprocessor directive: %s", t2s(tok));
}

/*----------------------------------------------------------------------
 * Special macros
 */

static struct tm *gettime(void) {
    if (current_time)
        return current_time;
    time_t timet = time(NULL);
    current_time = malloc(sizeof(struct tm));
    localtime_r(&timet, current_time);
    return current_time;
}

static void handle_date_macro(Token *tmpl) {
    Token *tok = copy_token(tmpl);
    tok->type = TSTRING;
    char *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct tm *now = gettime();
    tok->sval = format("%s %02d %04d", month[now->tm_mon], now->tm_mday, 1900 + now->tm_year);
    unget_token(tok);
}

static void handle_time_macro(Token *tmpl) {
    Token *tok = copy_token(tmpl);
    tok->type = TSTRING;
    struct tm *now = gettime();
    tok->sval = format("%02d:%02d:%02d", now->tm_hour, now->tm_min, now->tm_sec);
    unget_token(tok);
}

static void handle_file_macro(Token *tmpl) {
    Token *tok = copy_token(tmpl);
    tok->type = TSTRING;
    tok->sval = get_current_displayname();
    unget_token(tok);
}

static void handle_line_macro(Token *tmpl) {
    Token *tok = copy_token(tmpl);
    tok->type = TNUMBER;
    tok->sval = format("%d", get_current_line());
    unget_token(tok);
}

static void handle_pragma_macro(Token *ignore) {
    error("No pragmas supported");
}

static void handle_counter_macro(Token *tmpl) {
    Token *tok = copy_token(tmpl);
    tok->type = TNUMBER;
    tok->sval = format("%d", macro_counter++);
    unget_token(tok);
}

/*----------------------------------------------------------------------
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
    list_unshift(std_include_path, drop_last_slash(path));
}

/*----------------------------------------------------------------------
 * Initializer
 */

static void define_obj_macro(char *name, Token *value) {
    dict_put(macros, name, make_obj_macro(make_list1(value)));
}

static void define_special_macro(char *name, special_macro_handler *fn) {
    dict_put(macros, name, make_special_macro(fn));
}

void cpp_init(void) {
    list_unshift(std_include_path, "/usr/include/x86_64-linux-gnu");
    list_unshift(std_include_path, "/usr/include/linux");
    list_unshift(std_include_path, "/usr/include");
    list_unshift(std_include_path, "/usr/local/include");
    list_unshift(std_include_path, "./include");

    define_special_macro("__DATE__", handle_date_macro);
    define_special_macro("__TIME__", handle_time_macro);
    define_special_macro("__FILE__", handle_file_macro);
    define_special_macro("__LINE__", handle_line_macro);
    define_special_macro("_Pragma",  handle_pragma_macro);
    define_special_macro("__COUNTER__", handle_counter_macro);

    char *predefined[] = {
        "__8cc__", "__amd64", "__amd64__", "__x86_64", "__x86_64__",
        "linux", "__linux", "__linux__", "__gnu_linux__", "__unix", "__unix__",
        "_LP64", "__LP64__", "__ELF__", "__STDC__", "__STDC_HOSTED__" };

    for (int i = 0; i < sizeof(predefined) / sizeof(*predefined); i++)
        define_obj_macro(predefined[i], cpp_token_one);

    define_obj_macro("__STDC_VERSION__", make_number("199901L"));
    define_obj_macro("__SIZEOF_SHORT__", make_number("2"));
    define_obj_macro("__SIZEOF_INT__", make_number("4"));
    define_obj_macro("__SIZEOF_LONG__", make_number("8"));
    define_obj_macro("__SIZEOF_LONG_LONG__", make_number("8"));
    define_obj_macro("__SIZEOF_FLOAT__", make_number("4"));
    define_obj_macro("__SIZEOF_DOUBLE__", make_number("8"));
    define_obj_macro("__SIZEOF_LONG_DOUBLE__", make_number("16"));
    define_obj_macro("__SIZEOF_POINTER__", make_number("8"));
    define_obj_macro("__SIZEOF_PTRDIFF_T__", make_number("8"));
    define_obj_macro("__SIZEOF_SIZE_T__", make_number("8"));
}

/*----------------------------------------------------------------------
 * Keyword
 */

static Token *convert_punct(Token *tmpl, int punct) {
    Token *r = copy_token(tmpl);
    r->type = TPUNCT;
    r->punct = punct;
    return r;
}

static Token *maybe_convert_keyword(Token *tok) {
    if (!tok)
        return NULL;
    if (tok->type != TIDENT)
        return tok;
#define punct(ident, str)                       \
    if (!strcmp(str, tok->sval))                \
        return convert_punct(tok, ident);
#define keyword(ident, str, _)                  \
    if (!strcmp(str, tok->sval))                \
        return convert_punct(tok, ident);
#include "keyword.h"
#undef keyword
#undef punct
    return tok;
}

/*----------------------------------------------------------------------
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
        Token *tok = read_cpp_token();
        if (!tok)
            return NULL;
        if (tok && tok->type == TNEWLINE) {
            if (return_at_eol)
                return NULL;
            continue;
        }
        if (tok->bol && is_punct(tok, '#')) {
            read_directive();
            continue;
        }
        unget_token(tok);
        Token *r = read_expand();
        if (r && r->bol && is_punct(r, '#') && dict_empty(r->hideset)) {
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
        assert(tok->type != TNEWLINE);
        assert(tok->type != TSPACE);
        assert(tok->type != TMACRO_PARAM);
        if (tok->type != TSTRING)
            break;
        Token *tok2 = read_token_sub(false);
        if (!tok2 || tok2->type != TSTRING) {
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
