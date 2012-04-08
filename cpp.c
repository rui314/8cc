// For fmemopen()
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "8cc.h"

static Dict *macros = &EMPTY_DICT;
static List *cond_incl_stack = &EMPTY_LIST;
static List *std_include_path;
static Token *cpp_token_zero = &(Token){ .type = TTYPE_NUMBER, .sval = "0" };
static Token *cpp_token_one = &(Token){ .type = TTYPE_NUMBER, .sval = "1" };
static struct tm *current_time;

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
static Token *read_token_int(bool return_at_eol);
static Token *read_expand(void);
static void read_print(void);

/*----------------------------------------------------------------------
 * Eval
 */

static void eval(char *buf) {
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

static Token *make_macro_token(int position) {
    Token *r = malloc(sizeof(Token));
    r->type = TTYPE_MACRO_PARAM;
    r->hideset = make_dict(NULL);
    r->position = position;
    r->nspace = 0;
    r->bol = false;
    return r;
}

static Token *copy_token(Token *tok) {
    Token *r = malloc(sizeof(Token));
    memcpy(r, tok, sizeof(Token));
    return r;
}

static Token *make_number(char *s) {
    Token *tok = malloc(sizeof(Token));
    *tok = (Token){ TTYPE_NUMBER, .sval = s };
    return tok;
}

static void expect(char punct) {
    Token *tok = read_cpp_token();
    if (!tok || !is_punct(tok, punct))
        error("%c expected, but got %s", t2s(tok));
}

/*----------------------------------------------------------------------
 * Macro expander
 */

static Token *read_ident(void) {
    Token *r = read_cpp_token();
    if (r->type != TTYPE_IDENT)
        error("identifier expected, but got %s", t2s(r));
    return r;
}

void expect_newline(void) {
    Token *tok = read_cpp_token();
    if (!tok || tok->type != TTYPE_NEWLINE)
        error("Newline expected, but got %s", t2s(tok));
}

static List *read_args_int(Macro *macro) {
    Token *tok = read_cpp_token();
    if (!tok || !is_punct(tok, '(')) {
        unget_token(tok);
        return NULL;
    }
    List *r = make_list();
    List *arg = make_list();
    int depth = 0;
    for (;;) {
        tok = read_cpp_token();
        if (!tok)
            error("unterminated macro argument list");
        if (tok->type == TTYPE_NEWLINE)
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
    if (!args) return NULL;
    if (macro->is_varg && list_len(args) < macro->nargs)
        error("Macro argument number is less than expected");
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
    case TTYPE_IDENT:
    case TTYPE_NUMBER:
        string_appendf(s, "%s", tok->sval);
        return;
    case TTYPE_PUNCT:
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
    r->type = isdigit(get_cstring(s)[0]) ? TTYPE_NUMBER : TTYPE_IDENT;
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
        case TTYPE_IDENT:
        case TTYPE_NUMBER:
            string_appendf(s, "%s", tok->sval);
            break;
        case TTYPE_PUNCT:
            string_appendf(s, "%s", t2s(tok));
            break;
        case TTYPE_CHAR:
            string_appendf(s, "%s", quote_char(tok->c));
            break;
        case TTYPE_STRING:
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
    r->type = TTYPE_STRING;
    r->sval = join_tokens(args, true);
    return r;
}

static List *expand_all(List *tokens) {
    List *r = make_list();
    List *orig = get_input_buffer();
    set_input_buffer(tokens);
    Token *tok;
    while ((tok = read_expand()) != NULL)
        list_push(r, tok);
    set_input_buffer(orig);
    return r;
}

static List *subst(Macro *macro, List *args, Dict *hideset) {
    List *r = make_list();
    for (int i = 0; i < list_len(macro->body); i++) {
        bool islast = (i == list_len(macro->body) - 1);
        Token *t0 = list_get(macro->body, i);
        Token *t1 = islast ? NULL : list_get(macro->body, i + 1);
        bool t0_param = (t0->type == TTYPE_MACRO_PARAM);
        bool t1_param = (!islast && t1->type == TTYPE_MACRO_PARAM);

        if (is_punct(t0, '#') && t1_param) {
            list_push(r, stringize(t0, list_get(args, t1->position)));
            i++;
            continue;
        }
        if (is_ident(t0, "##") && t1_param) {
            List *arg = list_get(args, t1->position);
            if (list_len(arg) > 0) {
                glue_push(r, list_head(arg));
                List *tmp = list_copy(arg);
                list_shift(tmp);
                list_append(r, expand_all(tmp));
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
            list_append(r, expand_all(arg));
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
    if (tok->type == TTYPE_NEWLINE)
        return read_expand();
    if (tok->type != TTYPE_IDENT)
        return tok;
    char *name = tok->sval;
    Macro *macro = dict_get(macros, name);
    if (!macro || dict_get(tok->hideset, name))
        return tok;

    switch (macro->type) {
    case MACRO_OBJ: {
        Dict *hideset = dict_append(tok->hideset, name);
        List *tokens = subst(macro, make_list(), hideset);
        unget_all(tokens);
        return read_expand();
    }
    case MACRO_FUNC: {
        List *args = read_args(macro);
        Token *rparen = read_cpp_token();
        assert(is_punct(rparen, ')'));
        Dict *hideset = dict_append(dict_intersection(tok->hideset, rparen->hideset), name);
        List *tokens = subst(macro, args, hideset);
        unget_all(tokens);
        return read_expand();
    }
    case MACRO_SPECIAL: {
        special_macro_handler *fn = macro->fn;
        fn(tok);
        return read_expand();
    }
    default:
        error("internal error");
    }
}

static bool read_funclike_macro_args(Dict *param) {
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
        if (!tok || tok->type == TTYPE_NEWLINE)
            error("missing ')' in macro parameter list");
        if (is_ident(tok, "...")) {
            dict_put(param, "__VA_ARGS__", make_macro_token(pos++));
            expect(')');
            return true;
        }
        if (tok->type != TTYPE_IDENT)
            error("identifier expected, but got '%s'", t2s(tok));
        dict_put(param, tok->sval, make_macro_token(pos++));
    }
}

static List *read_funclike_macro_body(Dict *param) {
    List *r = make_list();
    for (;;) {
        Token *tok = read_cpp_token();
        if (!tok || tok->type == TTYPE_NEWLINE)
            return r;
        if (tok->type == TTYPE_IDENT) {
            Token *subst = dict_get(param, tok->sval);
            if (subst) {
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
    bool varg = read_funclike_macro_args(param);
    List *body = read_funclike_macro_body(param);
    Macro *macro = make_func_macro(body, list_len(dict_keys(param)), varg);
    dict_put(macros, name, macro);
}

static void read_obj_macro(char *name) {
    List *body = make_list();
    for (;;) {
        Token *tok = read_cpp_token();
        if (!tok || tok->type == TTYPE_NEWLINE)
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
    if (tok->type != TTYPE_IDENT)
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
        Token *tok = read_token_int(true);
        if (!tok) return r;
        if (is_ident(tok, "defined"))
            list_push(r, read_defined_op());
        else if (tok->type == TTYPE_IDENT)
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
    if (!tok || tok->type != TTYPE_IDENT)
        error("identifier expected, but got %s", t2s(tok));
    bool cond = !!dict_get(macros, tok->sval);
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
    if (!tok || tok->type == TTYPE_NEWLINE)
        error("expected file name, but got %s", t2s(tok));
    if (tok->type == TTYPE_STRING) {
        *std = false;
        return tok->sval;
    }
    if (!is_punct(tok, '<'))
        error("'<' expected, but got %s", t2s(tok));
    List *tokens = make_list();
    for (;;) {
        Token *tok = read_expand();
        if (!tok || tok->type == TTYPE_NEWLINE)
            error("premature end of header name");
        if (is_punct(tok, '>'))
            break;
        list_push(tokens, tok);
    }
    *std = true;
    return join_tokens(tokens, false);
}

static void read_include(void) {
    bool std;
    char *name = read_cpp_header_name(&std);
    expect_newline();
    List *paths;
    if (std) {
        paths = std_include_path;
    } else if (get_current_file()) {
        char *buf = format("%s", get_current_file());
        paths = make_list1(dirname(buf));
    } else {
        paths = make_list1(".");
    }
    for (Iter *i = list_iter(paths); !iter_end(i);) {
        char *path = format("%s/%s", iter_next(i), name);
        FILE *fp = fopen(path, "r");
        if (fp) {
            push_input_file(path, path, fp);
            return;
        }
    }
    error("Cannot find header file: %s", name);
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
    else if (is_ident(tok, "print"))   read_print();
    else if (tok->type != TTYPE_NEWLINE)
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
    tok->type = TTYPE_STRING;
    char *month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct tm *now = gettime();
    tok->sval = format("%s %02d %04d", month[now->tm_mon], now->tm_mday, 1900 + now->tm_year);
    unget_token(tok);
}

static void handle_time_macro(Token *tmpl) {
    Token *tok = copy_token(tmpl);
    tok->type = TTYPE_STRING;
    struct tm *now = gettime();
    tok->sval = format("%02d:%02d:%02d", now->tm_hour, now->tm_min, now->tm_sec);
    unget_token(tok);
}

static void handle_file_macro(Token *tmpl) {
    Token *tok = copy_token(tmpl);
    tok->type = TTYPE_STRING;
    tok->sval = get_current_file();
    unget_token(tok);
}

static void handle_line_macro(Token *tmpl) {
    Token *tok = copy_token(tmpl);
    tok->type = TTYPE_NUMBER;
    tok->sval = format("%d", get_current_line());
    unget_token(tok);
}

static void handle_pragma_macro(Token *ignore) {
    error("No pragmas supported");
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
    std_include_path = make_list();
    list_push(std_include_path, "/usr/local/include");
    list_push(std_include_path, "/usr/include");
    list_push(std_include_path, "/usr/include/linux");
    list_push(std_include_path, "/usr/include/x86_64-linux-gnu");
    list_push(std_include_path, "./include");

    define_special_macro("__DATE__", handle_date_macro);
    define_special_macro("__TIME__", handle_time_macro);
    define_special_macro("__FILE__", handle_file_macro);
    define_special_macro("__LINE__", handle_line_macro);
    define_special_macro("_Pragma", handle_pragma_macro);

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
    define_obj_macro("__SIZEOF_LONG_DOUBLE__", make_number("8"));
    define_obj_macro("__SIZEOF_POINTER__", make_number("8"));
    define_obj_macro("__SIZEOF_PTRDIFF_T__", make_number("8"));
    define_obj_macro("__SIZEOF_SIZE_T__", make_number("8"));

    eval("typedef long size_t;"
         "typedef long ptrdiff_t;"
         "typedef int wchar_t;"
         "typedef char _Bool;");
}

/*----------------------------------------------------------------------
 * Debugging functions
 */

static char *macro_to_string(char *name, Macro *m) {
    String *s = make_string();
    if (m->type == MACRO_OBJ)
        string_appendf(s, "%s ->", name, m->nargs);
    else
        string_appendf(s, "%s(%d) ->", name, m->nargs);
    for (Iter *i = list_iter(m->body); !iter_end(i);)
        string_appendf(s, " %s", t2s(iter_next(i)));
    return get_cstring(s);
}

static void read_print(void) {
    Token *tok = read_cpp_token();
    expect_newline();
    fprintf(stderr, "#print %s: ", input_position());
    if (tok->type == TTYPE_IDENT) {
        Macro *m = dict_get(macros, tok->sval);
        if (m) {
            fprintf(stderr, "%s\n", macro_to_string(tok->sval, m));
            return;
        }
    }
    fprintf(stderr, "%s\n", t2s(tok));
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

static Token *read_token_int(bool return_at_eol) {
    for (;;) {
        Token *tok = read_cpp_token();
        if (!tok)
            return NULL;
        if (tok && tok->type == TTYPE_NEWLINE) {
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
    for (;;) {
        Token *r = read_token_int(false);
        if (!r) return NULL;
        assert(r->type != TTYPE_NEWLINE);
        assert(r->type != TTYPE_SPACE);
        assert(r->type != TTYPE_MACRO_PARAM);
        if (r->type != TTYPE_STRING)
            return r;
        Token *r2 = read_token_int(false);
        if (!r2 || r2->type != TTYPE_STRING) {
            unget_token(r2);
            return r;
        }
        Token *conc = copy_token(r);
        conc->sval = format("%s%s", r->sval, r2->sval);
        unget_token(conc);
    }
}
