// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

/*
 * This implements Dave Prosser's C Preprocessing algorithm, described
 * in this document: https://github.com/rui314/8cc/wiki/cpp.algo.pdf
 */

#include <ctype.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "8cc.h"

static Map *macros = &EMPTY_MAP;
static Map *once = &EMPTY_MAP;
static Map *keywords = &EMPTY_MAP;
static Map *include_guard = &EMPTY_MAP;
static Vector *cond_incl_stack = &EMPTY_VECTOR;
static Vector *std_include_path = &EMPTY_VECTOR;
static struct tm now;
static Token *cpp_token_zero = &(Token){ .kind = TNUMBER, .sval = "0" };
static Token *cpp_token_one = &(Token){ .kind = TNUMBER, .sval = "1" };

typedef void SpecialMacroHandler(Token *tok);
typedef enum { IN_THEN, IN_ELIF, IN_ELSE } CondInclCtx;
typedef enum { MACRO_OBJ, MACRO_FUNC, MACRO_SPECIAL } MacroType;

typedef struct {
    CondInclCtx ctx;
    char *include_guard;
    File *file;
    bool wastrue;
} CondIncl;

typedef struct {
    MacroType kind;
    int nargs;
    Vector *body;
    bool is_varg;
    SpecialMacroHandler *fn;
} Macro;

static Macro *make_obj_macro(Vector *body);
static Macro *make_func_macro(Vector *body, int nargs, bool is_varg);
static Macro *make_special_macro(SpecialMacroHandler *fn);
static void define_obj_macro(char *name, Token *value);
static void read_directive(void);
static Token *read_expand(void);

/*
 * Eval
 */

void cpp_eval(char *buf) {
    stream_stash(make_file_string(buf));
    Vector *toplevels = read_toplevels();
    for (int i = 0; i < vec_len(toplevels); i++)
        emit_toplevel(vec_get(toplevels, i));
    stream_unstash();
}

/*
 * Constructors
 */

static CondIncl *make_cond_incl(bool wastrue) {
    CondIncl *r = malloc(sizeof(CondIncl));
    r->ctx = IN_THEN;
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

static Macro *make_special_macro(SpecialMacroHandler *fn) {
    return make_macro(&(Macro){ MACRO_SPECIAL, .fn = fn });
}

static Token *make_macro_token(int position, bool is_vararg) {
    Token *r = malloc(sizeof(Token));
    r->kind = TMACRO_PARAM;
    r->is_vararg = is_vararg;
    r->hideset = NULL;
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

static void expect(char id) {
    Token *tok = lex();
    if (!is_keyword(tok, id))
        error("%c expected, but got %s", id, tok2s(tok));
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

static void propagate_space(Vector *tokens, Token *tmpl) {
    if (vec_len(tokens) == 0)
        return;
    Token *tok = copy_token(vec_head(tokens));
    tok->space = tmpl->space;
    vec_set(tokens, 0, tok);
}

/*
 * Macro expander
 */

static Token *read_ident(void) {
    Token *r = lex();
    if (r->kind != TIDENT)
        error("identifier expected, but got %s", tok2s(r));
    return r;
}

void expect_newline(void) {
    Token *tok = lex();
    if (tok->kind != TNEWLINE)
        error("newline expected, but got %s", tok2s(tok));
}

static Vector *read_one_arg(bool *end, bool readall) {
    Vector *r = make_vector();
    int level = 0;
    for (;;) {
        Token *tok = lex();
        if (tok->kind == TEOF)
            error("unterminated macro argument list");
        if (tok->kind == TNEWLINE)
            continue;
        if (tok->bol && is_keyword(tok, '#')) {
            read_directive();
            continue;
        }
        if (level == 0 && is_keyword(tok, ')')) {
            unget_token(tok);
            *end = true;
            return r;
        }
        if (level == 0 && is_keyword(tok, ',') && !readall)
            return r;
        if (is_keyword(tok, '('))
            level++;
        if (is_keyword(tok, ')'))
            level--;
        if (tok->bol) {
            tok->bol = false;
            tok->space = true;
        }
        vec_push(r, tok);
    }
}

static Vector *do_read_args(Macro *macro) {
    Vector *r = make_vector();
    bool end = false;
    while (!end) {
        bool in_ellipsis = (macro->is_varg && vec_len(r) + 1 == macro->nargs);
        vec_push(r, read_one_arg(&end, in_ellipsis));
    }
    if (macro->is_varg && vec_len(r) == macro->nargs - 1)
        vec_push(r, make_vector());
    return r;
}

static Vector *read_args(Macro *macro) {
    if (macro->nargs == 0 && is_keyword(peek_token(), ')')) {
        // If a macro M has no parameter, argument list of M()
        // is an empty list. If it has one parameter,
        // argument list of M() is a list containing an empty list.
        return make_vector();
    }
    Vector *args = do_read_args(macro);
    if (vec_len(args) != macro->nargs)
        error("macro argument number does not match");
    return args;
}

static Vector *add_hide_set(Vector *tokens, Set *hideset) {
    Vector *r = make_vector();
    for (int i = 0; i < vec_len(tokens); i++) {
        Token *t = copy_token(vec_get(tokens, i));
        t->hideset = set_union(t->hideset, hideset);
        vec_push(r, t);
    }
    return r;
}

static Token *glue_tokens(Token *t, Token *u) {
    Buffer *b = make_buffer();
    buf_printf(b, "%s", tok2s(t));
    buf_printf(b, "%s", tok2s(u));
    Token *r = lex_string(buf_body(b));
    return r;
}

static void glue_push(Vector *tokens, Token *tok) {
    Token *last = vec_pop(tokens);
    vec_push(tokens, glue_tokens(last, tok));
}

static Token *stringize(Token *tmpl, Vector *args) {
    Buffer *b = make_buffer();
    for (int i = 0; i < vec_len(args); i++) {
        Token *tok = vec_get(args, i);
        if (buf_len(b) && tok->space)
            buf_printf(b, " ");
        buf_printf(b, "%s", tok2s(tok));
    }
    buf_write(b, '\0');
    Token *r = copy_token(tmpl);
    r->kind = TSTRING;
    r->sval = buf_body(b);
    r->slen = buf_len(b);
    r->enc = ENC_NONE;
    return r;
}

static Vector *expand_all(Vector *tokens, Token *tmpl) {
    push_token_buffer(vec_reverse(tokens));
    Vector *r = make_vector();
    for (;;) {
        Token *tok = read_expand();
        if (tok->kind == TEOF)
            break;
        vec_push(r, tok);
    }
    propagate_space(r, tmpl);
    pop_token_buffer();
    return r;
}

static Vector *subst(Macro *macro, Vector *args, Set *hideset) {
    Vector *r = make_vector();
    int len = vec_len(macro->body);
    for (int i = 0; i < len; i++) {
        Token *t0 = vec_get(macro->body, i);
        Token *t1 = (i == len - 1) ? NULL : vec_get(macro->body, i + 1);
        bool t0_param = (t0->kind == TMACRO_PARAM);
        bool t1_param = (t1 && t1->kind == TMACRO_PARAM);

        if (is_keyword(t0, '#') && t1_param) {
            vec_push(r, stringize(t0, vec_get(args, t1->position)));
            i++;
            continue;
        }
        if (is_keyword(t0, KHASHHASH) && t1_param) {
            Vector *arg = vec_get(args, t1->position);
            // [GNU] [,##__VA_ARG__] is expanded to the empty token sequence
            // if __VA_ARG__ is empty. Otherwise it's expanded to
            // [,<tokens in __VA_ARG__>].
            if (t1->is_vararg && vec_len(r) > 0 && is_keyword(vec_tail(r), ',')) {
                if (vec_len(arg) > 0)
                    vec_append(r, arg);
                else
                    vec_pop(r);
            } else if (vec_len(arg) > 0) {
                glue_push(r, vec_head(arg));
                for (int i = 1; i < vec_len(arg); i++)
                    vec_push(r, vec_get(arg, i));
            }
            i++;
            continue;
        }
        if (is_keyword(t0, KHASHHASH) && t1) {
            hideset = t1->hideset;
            glue_push(r, t1);
            i++;
            continue;
        }
        if (t0_param && t1 && is_keyword(t1, KHASHHASH)) {
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

// This is "expand" function in the Dave Prosser's document.
static Token *read_expand_newline(void) {
    Token *tok = lex();
    if (tok->kind == TEOF)
        return tok;
    if (tok->kind != TIDENT)
        return tok;
    char *name = tok->sval;
    Macro *macro = map_get(macros, name);
    if (!macro || set_has(tok->hideset, name))
        return tok;

    switch (macro->kind) {
    case MACRO_OBJ: {
        Set *hideset = set_add(tok->hideset, name);
        Vector *tokens = subst(macro, NULL, hideset);
        propagate_space(tokens, tok);
        unget_all(tokens);
        return read_expand();
    }
    case MACRO_FUNC: {
        if (!next('('))
            return tok;
        Vector *args = read_args(macro);
        Token *rparen = peek_token();
        expect(')');
        Set *hideset = set_add(set_intersection(tok->hideset, rparen->hideset), name);
        Vector *tokens = subst(macro, args, hideset);
        propagate_space(tokens, tok);
        unget_all(tokens);
        return read_expand();
    }
    case MACRO_SPECIAL:
        macro->fn(tok);
        return read_expand();
    default:
        error("internal error");
    }
}

static Token *read_expand(void) {
    for (;;) {
        Token *tok = read_expand_newline();
        if (tok->kind != TNEWLINE)
            return tok;
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
                error("',' expected, but got '%s'", tok2s(tok));
            tok = lex();
        }
        if (tok->kind == TNEWLINE)
            error("missing ')' in macro parameter list");
        if (is_keyword(tok, KELLIPSIS)) {
            map_put(param, "__VA_ARGS__", make_macro_token(pos++, true));
            expect(')');
            return true;
        }
        if (tok->kind != TIDENT)
            error("identifier expected, but got '%s'", tok2s(tok));
        char *arg = tok->sval;
        if (next(KELLIPSIS)) {
            expect(')');
            map_put(param, arg, make_macro_token(pos++, true));
            return true;
        }
        map_put(param, arg, make_macro_token(pos++, false));
    }
}

static void hashhash_check(Vector *v) {
    if (vec_len(v) == 0)
        return;
    if (is_keyword(vec_head(v), KHASHHASH))
        error("'##' cannot appear at start of macro expansion");
    if (is_keyword(vec_tail(v), KHASHHASH))
        error("'##' cannot appear at end of macro expansion");
}

static Vector *read_funclike_macro_body(Map *param) {
    Vector *r = make_vector();
    for (;;) {
        Token *tok = lex();
        if (tok->kind == TNEWLINE)
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
}

static void read_funclike_macro(char *name) {
    Map *param = make_map();
    bool is_varg = read_funclike_macro_params(param);
    Vector *body = read_funclike_macro_body(param);
    hashhash_check(body);
    Macro *macro = make_func_macro(body, map_len(param), is_varg);
    map_put(macros, name, macro);
}

static void read_obj_macro(char *name) {
    Vector *body = make_vector();
    for (;;) {
        Token *tok = lex();
        if (tok->kind == TNEWLINE)
            break;
        vec_push(body, tok);
    }
    hashhash_check(body);
    map_put(macros, name, make_obj_macro(body));
}

/*
 * #define
 */

static void read_define(void) {
    Token *name = read_ident();
    Token *tok = lex();
    if (is_keyword(tok, '(') && !tok->space) {
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
 * #if and the like
 */

static Token *read_defined_op(void) {
    Token *tok = lex();
    if (is_keyword(tok, '(')) {
        tok = lex();
        expect(')');
    }
    if (tok->kind != TIDENT)
        error("Identifier expected, but got %s", tok2s(tok));
    return map_get(macros, tok->sval) ? cpp_token_one : cpp_token_zero;
}

static Vector *read_intexpr_line(void) {
    Vector *r = make_vector();
    for (;;) {
        Token *tok = read_expand_newline();
        if (tok->kind == TNEWLINE)
            return r;
        if (is_ident(tok, "defined")) {
            vec_push(r, read_defined_op());
        } else if (tok->kind == TIDENT) {
            // C11 6.10.1.4 says that remaining identifiers
            // should be replaced with pp-number 0.
            vec_push(r, cpp_token_zero);
        } else {
            vec_push(r, tok);
        }
    }
}

static bool read_constexpr(void) {
    push_token_buffer(vec_reverse(read_intexpr_line()));
    Node *expr = read_expr();
    Token *tok = lex();
    if (tok->kind != TEOF)
        error("Stray token: %s", tok2s(tok));
    pop_token_buffer();
    return eval_intexpr(expr);
}

static void do_read_if(bool istrue) {
    vec_push(cond_incl_stack, make_cond_incl(istrue));
    if (!istrue)
        skip_cond_incl();
}

static void read_if(void) {
    do_read_if(read_constexpr());
}

static void read_ifdef(void) {
    Token *tok = lex();
    if (tok->kind != TIDENT)
        error("identifier expected, but got %s", tok2s(tok));
    expect_newline();
    do_read_if(map_get(macros, tok->sval));
}

static void read_ifndef(void) {
    Token *tok = lex();
    if (tok->kind != TIDENT)
        error("identifier expected, but got %s", tok2s(tok));
    expect_newline();
    do_read_if(!map_get(macros, tok->sval));
    if (tok->count == 2) {
        // "ifndef" is the second token in this file.
        // Prepare to detect an include guard.
        CondIncl *ci = vec_tail(cond_incl_stack);
        ci->include_guard = tok->sval;
        ci->file = tok->file;
    }
}

static void read_else(void) {
    if (vec_len(cond_incl_stack) == 0)
        error("stray #else");
    CondIncl *ci = vec_tail(cond_incl_stack);
    if (ci->ctx == IN_ELSE)
        error("#else appears in #else");
    expect_newline();
    ci->ctx = IN_ELSE;
    ci->include_guard = NULL;
    if (ci->wastrue)
        skip_cond_incl();
}

static void read_elif(void) {
    if (vec_len(cond_incl_stack) == 0)
        error("stray #elif");
    CondIncl *ci = vec_tail(cond_incl_stack);
    if (ci->ctx == IN_ELSE)
        error("#elif after #else");
    ci->ctx = IN_ELIF;
    ci->include_guard = NULL;
    if (ci->wastrue || !read_constexpr()) {
        skip_cond_incl();
        return;
    }
    ci->wastrue = true;
}

static void skip_newlines(void) {
    // Skip all but the last newline.
    Token *tok = lex();
    while (is_keyword(tok, TNEWLINE) && is_keyword(peek_token(), TNEWLINE))
        tok = lex();
    unget_token(tok);
}

static void read_endif(void) {
    if (vec_len(cond_incl_stack) == 0)
        error("stray #endif");
    CondIncl *ci = vec_tail(cond_incl_stack);
    vec_pop(cond_incl_stack);
    expect_newline();

    // Detect an #ifndef and #endif pair that guards the entire
    // header file. Remember the macro name guarding the file
    // so that we can skip the file next time.
    if (!ci->include_guard || ci->file != current_file())
        return;
    skip_newlines();
    if (ci->file != current_file())
        map_put(include_guard, ci->file->name, ci->include_guard);
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

static char *join_paths(Vector *args) {
    Buffer *b = make_buffer();
    for (int i = 0; i < vec_len(args); i++)
        buf_printf(b, "%s", tok2s(vec_get(args, i)));
    return buf_body(b);
}

static char *read_cpp_header_name(bool *std) {
    // Filename after #include needs a special tokenization treatment.
    // It may be quoted by < and > instead of "". Even if it's quoted
    // by "", it's still different from a regular string token.
    // For example, \ in a file name should not be interpreted as a
    // quote. (Think of Windows path.)
    char *path = read_header_file_name(std);
    if (path)
        return path;

    // If a token following #include does not start with < nor ",
    // try to read the token as a regular token. Macro-expanded
    // form may be a valid header file path.
    Token *tok = read_expand_newline();
    if (tok->kind == TNEWLINE)
        error("expected file name, but got %s", tok2s(tok));
    if (tok->kind == TSTRING) {
        *std = false;
        return tok->sval;
    }
    if (!is_keyword(tok, '<'))
        error("'<' expected, but got %s", tok2s(tok));
    Vector *tokens = make_vector();
    for (;;) {
        Token *tok = read_expand_newline();
        if (tok->kind == TNEWLINE)
            error("premature end of header name");
        if (is_keyword(tok, '>'))
            break;
        vec_push(tokens, tok);
    }
    *std = true;
    return join_paths(tokens);
}

static bool guarded(char *path) {
    char *guard = map_get(include_guard, path);
    bool r = (guard && map_get(macros, guard));
    define_obj_macro("__8cc_include_guard", r ? cpp_token_one : cpp_token_zero);
    return r;
}

static bool try_include(char *dir, char *filename, bool isimport) {
    char *path = fullpath(format("%s/%s", dir, filename));
    if (map_get(once, path))
        return true;
    if (guarded(path))
        return true;
    FILE *fp = fopen(path, "r");
    if (!fp)
        return false;
    if (isimport)
        map_put(once, path, (void *)1);
    stream_push(make_file(fp, path));
    return true;
}

static void read_include(bool isimport) {
    bool std;
    char *filename = read_cpp_header_name(&std);
    expect_newline();
    if (filename[0] == '/') {
        if (try_include("/", filename, isimport))
            return;
        goto err;
    }
    if (!std) {
        File *f = current_file();
        char *dir = f->name ? dirname(strdup(f->name)) : ".";
        if (try_include(dir, filename, isimport))
            return;
    }
    for (int i = vec_len(std_include_path) - 1; i >= 0; i--)
        if (try_include(vec_get(std_include_path, i), filename, isimport))
            return;
  err:
    error("cannot find header file: %s", filename);
}

static void read_include_next(void) {
    // [GNU] #include_next is a directive to include the "next" file
    // from the search path. This feature is used to override a
    // header file without getting into infinite inclusion loop.
    // This directive doesn't distinguish <> and "".
    bool std;
    char *filename = read_cpp_header_name(&std);
    expect_newline();
    if (filename[0] == '/') {
        if (try_include("/", filename, false))
            return;
        goto err;
    }
    char *cur = fullpath(current_file()->name);
    int i = vec_len(std_include_path) - 1;
    for (; i >= 0; i--) {
        char *dir = vec_get(std_include_path, i);
        if (!strcmp(cur, fullpath(format("%s/%s", dir, filename))))
            break;
    }
    for (i--; i >= 0; i--)
        if (try_include(vec_get(std_include_path, i), filename, false))
            return;
  err:
    error("cannot find header file: %s", filename);
}

/*
 * #pragma
 */

static void parse_pragma_operand(char *s) {
    if (!strcmp(s, "once")) {
        char *path = fullpath(current_file()->name);
        map_put(once, path, (void *)1);
    } else if (!strcmp(s, "enable_warning")) {
        enable_warning = true;
    } else if (!strcmp(s, "disable_warning")) {
        enable_warning = false;
    } else {
        error("Unknown #pragma: %s", s);
    }
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
    Token *tok = read_expand_newline();
    if (tok->kind != TNUMBER || !is_digit_sequence(tok->sval))
        error("number expected after #line, but got %s", tok2s(tok));
    int line = atoi(tok->sval);
    tok = read_expand_newline();
    char *filename = NULL;
    if (tok->kind == TSTRING) {
        filename = tok->sval;
        expect_newline();
    } else if (tok->kind != TNEWLINE) {
        error("newline or a source name are expected, but got %s", tok2s(tok));
    }
    File *f = current_file();
    f->line = line;
    if (filename)
        f->name = filename;
}

// GNU CPP outputs "# linenum filename flags" to preserve original
// source file information. This function reads them. Flags are ignored.
static void read_linemarker(Token *tok) {
    if (!is_digit_sequence(tok->sval))
        error("line number expected, but got %s", tok2s(tok));
    int line = atoi(tok->sval);
    tok = lex();
    if (tok->kind != TSTRING)
        error("file name expected, but got %s", tok2s(tok));
    char *filename = tok->sval;
    do {
        tok = lex();
    } while (tok->kind != TNEWLINE);
    File *file = current_file();
    file->line = line;
    file->name = filename;
}

/*
 * #-directive
 */

static void read_directive(void) {
    Token *tok = lex();
    if (tok->kind == TNEWLINE)
        return;
    if (tok->kind == TNUMBER) {
        read_linemarker(tok);
        return;
    }
    if (tok->kind != TIDENT)
        goto err;
    char *s = tok->sval;
    if (!strcmp(s, "define"))            read_define();
    else if (!strcmp(s, "elif"))         read_elif();
    else if (!strcmp(s, "else"))         read_else();
    else if (!strcmp(s, "endif"))        read_endif();
    else if (!strcmp(s, "error"))        read_error();
    else if (!strcmp(s, "if"))           read_if();
    else if (!strcmp(s, "ifdef"))        read_ifdef();
    else if (!strcmp(s, "ifndef"))       read_ifndef();
    else if (!strcmp(s, "import"))       read_include(true);
    else if (!strcmp(s, "include"))      read_include(false);
    else if (!strcmp(s, "include_next")) read_include_next();
    else if (!strcmp(s, "line"))         read_line();
    else if (!strcmp(s, "pragma"))       read_pragma();
    else if (!strcmp(s, "undef"))        read_undef();
    else if (!strcmp(s, "warning"))      read_warning();
    else goto err;
    return;

  err:
    error("unsupported preprocessor directive: %s", tok2s(tok));
}

/*
 * Special macros
 */

static void make_token_pushback(Token *tmpl, int kind, char *sval) {
    Token *tok = copy_token(tmpl);
    tok->kind = kind;
    tok->sval = sval;
    tok->slen = strlen(sval) + 1;
    tok->enc = ENC_NONE;
    unget_token(tok);
}

static void handle_date_macro(Token *tmpl) {
    char *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char *sval = format("%s %2d %04d", month[now.tm_mon], now.tm_mday, 1900 + now.tm_year);
    make_token_pushback(tmpl, TSTRING, sval);
}

static void handle_time_macro(Token *tmpl) {
    char *sval = format("%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
    make_token_pushback(tmpl, TSTRING, sval);
}

static void handle_timestamp_macro(Token *tmpl) {
    char buf[30];
    asctime_r(&now, buf);
    // Remove the trailing '\n'.
    buf[strlen(buf) - 1] = '\0';
    make_token_pushback(tmpl, TSTRING, strdup(buf));
}

static void handle_file_macro(Token *tmpl) {
    File *f = current_file();
    make_token_pushback(tmpl, TSTRING, f->name);
}

static void handle_line_macro(Token *tmpl) {
    File *f = current_file();
    make_token_pushback(tmpl, TNUMBER, format("%d", f->line));
}

static void handle_pragma_macro(Token *tmpl) {
    expect('(');
    Token *operand = read_token();
    if (operand->kind != TSTRING)
        error("_Pragma takes a string literal, but got %s", tok2s(operand));
    expect(')');
    parse_pragma_operand(operand->sval);
    make_token_pushback(tmpl, TNUMBER, "1");
}

static void handle_base_file_macro(Token *tmpl) {
    make_token_pushback(tmpl, TSTRING, get_base_file());
}

static void handle_counter_macro(Token *tmpl) {
    static int counter = 0;
    make_token_pushback(tmpl, TNUMBER, format("%d", counter++));
}

static void handle_include_level_macro(Token *tmpl) {
    make_token_pushback(tmpl, TNUMBER, format("%d", stream_depth() - 1));
}

/*
 * Initializer
 */

void add_include_path(char *path) {
    vec_push(std_include_path, path);
}

static void define_obj_macro(char *name, Token *value) {
    map_put(macros, name, make_obj_macro(make_vector1(value)));
}

static void define_special_macro(char *name, SpecialMacroHandler *fn) {
    map_put(macros, name, make_special_macro(fn));
}

static void init_keywords(void) {
#define op(id, str)         map_put(keywords, str, (void *)id);
#define keyword(id, str, _) map_put(keywords, str, (void *)id);
#include "keyword.inc"
#undef keyword
#undef op
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
    // [GNU] Non-standard macros
    define_special_macro("__BASE_FILE__", handle_base_file_macro);
    define_special_macro("__COUNTER__", handle_counter_macro);
    define_special_macro("__INCLUDE_LEVEL__", handle_include_level_macro);
    define_special_macro("__TIMESTAMP__", handle_timestamp_macro);

    cpp_eval("#include <" BUILD_DIR "/include/8cc.h>");
}

void init_now(void) {
    time_t timet = time(NULL);
    localtime_r(&timet, &now);
}

void cpp_init(void) {
    init_keywords();
    init_now();
    init_predefined_macros();
}

/*
 * Public intefaces
 */

static Token *maybe_convert_keyword(Token *tok) {
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

Token *peek_token(void) {
    Token *r = read_token();
    unget_token(r);
    return r;
}

Token *read_token(void) {
    Token *tok;
    for (;;) {
        tok = read_expand();
        if (tok->bol && is_keyword(tok, '#') && tok->hideset == NULL) {
            read_directive();
            continue;
        }
        assert(tok->kind < MIN_CPP_TOKEN);
        return maybe_convert_keyword(tok);
    }
}
