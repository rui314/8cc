// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

/*
 * Recursive descendent parser for C.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "8cc.h"

// The largest alignment requirement on x86-64. When we are allocating memory
// for an array whose type is unknown, the array will be aligned to this
// boundary.
#define MAX_ALIGN 16

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

// The last source location we want to point to when we find an error in the
// soruce code.
SourceLoc *source_loc;

// Objects representing various scopes. Did you know C has so many different
// scopes? You can use the same name for global variable, local variable, struct
// tag, union tag, and goto label!
static Map *globalenv = &EMPTY_MAP;
static Map *localenv;
static Map *struct_defs = &EMPTY_MAP;
static Map *union_defs = &EMPTY_MAP;
static Map *labels;

static Vector *toplevels;
static Vector *localvars;
static Vector *gotos;
static Type *current_func_type;

// Objects representing basic types. All variables will be of one of these types
// or a derived type from one of them. Note that (typename){initializer} is C99
// feature to write a literal struct.
Type *type_void = &(Type){ KIND_VOID, 0, 0, false };
Type *type_bool = &(Type){ KIND_BOOL, 1, 1, true };
Type *type_char = &(Type){ KIND_CHAR, 1, 1, false };
Type *type_short = &(Type){ KIND_SHORT, 2, 2, false };
Type *type_int = &(Type){ KIND_INT, 4, 4, false };
Type *type_long = &(Type){ KIND_LONG, 8, 8, false };
Type *type_llong = &(Type){ KIND_LLONG, 8, 8, false };
Type *type_uchar = &(Type){ KIND_CHAR, 1, 1, true };
Type *type_ushort = &(Type){ KIND_SHORT, 2, 2, true };
Type *type_uint = &(Type){ KIND_INT, 4, 4, true };
Type *type_ulong = &(Type){ KIND_LONG, 8, 8, true };
Type *type_ullong = &(Type){ KIND_LLONG, 8, 8, true };
Type *type_float = &(Type){ KIND_FLOAT, 4, 4, false };
Type *type_double = &(Type){ KIND_DOUBLE, 8, 8, false };
Type *type_ldouble = &(Type){ KIND_LDOUBLE, 8, 8, false };

static Type* make_ptr_type(Type *ty);
static Type* make_array_type(Type *ty, int size);
static Node *read_compound_stmt(void);
static void read_decl_or_stmt(Vector *list);
static Node *conv(Node *node);
static Node *read_stmt(void);
static bool is_type_keyword(Token *tok);
static Node *read_unary_expr(void);
static Type *read_func_param(char **name, bool optional);
static void read_decl(Vector *toplevel, bool isglobal);
static Type *read_declarator(char **name, Type *basetype, Vector *params, int ctx);
static Type *read_decl_spec(int *sclass);
static Node *read_struct_field(Node *struc);
static void read_initializer_list(Vector *inits, Type *ty, int off, bool designated);
static Type *read_cast_type(void);
static Vector *read_decl_init(Type *ty);
static Node *read_boolean_expr(void);
static Node *read_expr_opt(void);
static Node *read_conditional_expr(void);
static Node *read_assignment_expr(void);
static Node *read_cast_expr(void);
static Node *read_comma_expr(void);

enum {
    S_TYPEDEF = 1,
    S_EXTERN,
    S_STATIC,
    S_AUTO,
    S_REGISTER,
};

enum {
    DECL_BODY = 1,
    DECL_PARAM,
    DECL_PARAM_TYPEONLY,
    DECL_CAST,
};

/*
 * Source location
 */

static void mark_location(void) {
    Token *tok = peek_token();
    if (!tok) {
        source_loc = NULL;
        return;
    }
    source_loc = malloc(sizeof(SourceLoc));
    source_loc->file = tok->file;
    source_loc->line = tok->line;
}

/*
 * Constructors
 */

char *make_label(void) {
    static int c = 0;
    return format(".L%d", c++);
}

static char *make_static_label(char *name) {
    static int c = 0;
    return format(".S%d.%s", c++, name);
}

static Map *env(void) {
    return localenv ? localenv : globalenv;
}

static Node *make_ast(Node *tmpl) {
    Node *r = malloc(sizeof(Node));
    *r = *tmpl;
    r->sourceLoc = source_loc;
    return r;
}

static Node *ast_uop(int kind, Type *ty, Node *operand) {
    return make_ast(&(Node){ kind, ty, .operand = operand });
}

static Node *ast_binop(Type *ty, int kind, Node *left, Node *right) {
    Node *r = make_ast(&(Node){ kind, ty });
    r->left = left;
    r->right = right;
    return r;
}

static Node *ast_inttype(Type *ty, long val) {
    return make_ast(&(Node){ AST_LITERAL, ty, .ival = val });
}

static Node *ast_floattype(Type *ty, double val) {
    return make_ast(&(Node){ AST_LITERAL, ty, .fval = val });
}

static Node *ast_lvar(Type *ty, char *name) {
    Node *r = make_ast(&(Node){ AST_LVAR, ty, .varname = name });
    if (localenv)
        map_put(localenv, name, r);
    if (localvars)
        vec_push(localvars, r);
    return r;
}

static Node *ast_gvar(Type *ty, char *name) {
    Node *r = make_ast(&(Node){ AST_GVAR, ty, .varname = name, .glabel = name });
    map_put(globalenv, name, r);
    return r;
}

static Node *ast_static_lvar(Type *ty, char *name) {
    Node *r = make_ast(&(Node){
        .kind = AST_GVAR,
        .ty = ty,
        .varname = name,
        .glabel = make_static_label(name) });
    assert(localenv);
    map_put(localenv, name, r);
    return r;
}

static Node *ast_typedef(Type *ty, char *name) {
    Node *r = make_ast(&(Node){ AST_TYPEDEF, ty, .typedefname = name });
    map_put(env(), name, r);
    return r;
}

static Node *ast_string(char *str) {
    return make_ast(&(Node){
        .kind = AST_STRING,
        .ty = make_array_type(type_char, strlen(str) + 1),
        .sval = str });
}

static Node *ast_funcall(Type *ftype, char *fname, Vector *args) {
    return make_ast(&(Node){
        .kind = AST_FUNCALL,
        .ty = ftype->rettype,
        .fname = fname,
        .args = args,
        .ftype = ftype });
}

static Node *ast_funcdesg(char *fname, Node *func) {
    return make_ast(&(Node){
        .kind = AST_FUNCDESG,
        .ty = type_void,
        .fname = fname,
        .fptr = func });
}

static Node *ast_funcptr_call(Node *fptr, Vector *args) {
    assert(fptr->ty->kind == KIND_PTR);
    assert(fptr->ty->ptr->kind == KIND_FUNC);
    return make_ast(&(Node){
        .kind = AST_FUNCPTR_CALL,
        .ty = fptr->ty->ptr->rettype,
        .fptr = fptr,
        .args = args });
}

static Node *ast_func(Type *ty, char *fname, Vector *params, Node *body, Vector *localvars) {
    return make_ast(&(Node){
        .kind = AST_FUNC,
        .ty = ty,
        .fname = fname,
        .params = params,
        .localvars = localvars,
        .body = body});
}

static Node *ast_decl(Node *var, Vector *init) {
    return make_ast(&(Node){ AST_DECL, .declvar = var, .declinit = init });
}

static Node *ast_init(Node *val, Type *totype, int off) {
    return make_ast(&(Node){ AST_INIT, .initval = val, .initoff = off, .totype = totype });
}

static Node *ast_conv(Type *totype, Node *val) {
    return make_ast(&(Node){ AST_CONV, totype, .operand = val });
}

static Node *ast_if(Node *cond, Node *then, Node *els) {
    return make_ast(&(Node){ AST_IF, .cond = cond, .then = then, .els = els });
}

static Node *ast_ternary(Type *ty, Node *cond, Node *then, Node *els) {
    return make_ast(&(Node){ AST_TERNARY, ty, .cond = cond, .then = then, .els = els });
}

static Node *ast_for(Node *init, Node *cond, Node *step, Node *body) {
    return make_ast(&(Node){
            AST_FOR, .forinit = init, .forcond = cond, .forstep = step, .forbody = body });
}

static Node *ast_while(Node *cond, Node *body) {
    return make_ast(&(Node){ AST_WHILE, .forcond = cond, .forbody = body });
}

static Node *ast_do(Node *cond, Node *body) {
    return make_ast(&(Node){ AST_DO, .forcond = cond, .forbody = body });
}

static Node *ast_switch(Node *expr, Node *body) {
    return make_ast(&(Node){ AST_SWITCH, .switchexpr = expr, .switchbody = body });
}

static Node *ast_case(int begin, int end) {
    return make_ast(&(Node){ AST_CASE, .casebeg = begin, .caseend = end });
}

static Node *ast_return(Node *retval) {
    return make_ast(&(Node){ AST_RETURN, .retval = retval });
}

static Node *ast_compound_stmt(Vector *stmts) {
    return make_ast(&(Node){ AST_COMPOUND_STMT, .stmts = stmts });
}

static Node *ast_struct_ref(Type *ty, Node *struc, char *name) {
    return make_ast(&(Node){ AST_STRUCT_REF, ty, .struc = struc, .field = name });
}

static Node *ast_goto(char *label) {
    return make_ast(&(Node){ AST_GOTO, .label = label });
}

static Node *ast_computed_goto(Node *expr) {
    return make_ast(&(Node){ AST_COMPUTED_GOTO, .operand = expr });
}

static Node *ast_label(char *label) {
    return make_ast(&(Node){ AST_LABEL, .label = label });
}

static Node *ast_va_start(Node *ap) {
    return make_ast(&(Node){ AST_VA_START, type_void, .ap = ap });
}

static Node *ast_va_arg(Type *ty, Node *ap) {
    return make_ast(&(Node){ AST_VA_ARG, ty, .ap = ap });
}

static Node *ast_label_addr(char *label) {
    return make_ast(&(Node){ OP_LABEL_ADDR, make_ptr_type(type_void), .label = label });
}

static Type *make_type(Type *tmpl) {
    Type *r = malloc(sizeof(Type));
    *r = *tmpl;
    return r;
}

static Type *copy_type(Type *ty) {
    Type *r = malloc(sizeof(Type));
    memcpy(r, ty, sizeof(Type));
    return r;
}

static Type *make_numtype(int kind, bool usig) {
    Type *r = malloc(sizeof(Type));
    r->kind = kind;
    r->usig = usig;
    if (kind == KIND_VOID)         r->size = r->align = 0;
    else if (kind == KIND_BOOL)    r->size = r->align = 1;
    else if (kind == KIND_CHAR)    r->size = r->align = 1;
    else if (kind == KIND_SHORT)   r->size = r->align = 2;
    else if (kind == KIND_INT)     r->size = r->align = 4;
    else if (kind == KIND_LONG)    r->size = r->align = 8;
    else if (kind == KIND_LLONG)   r->size = r->align = 8;
    else if (kind == KIND_FLOAT)   r->size = r->align = 4;
    else if (kind == KIND_DOUBLE)  r->size = r->align = 8;
    else if (kind == KIND_LDOUBLE) r->size = r->align = 8;
    else error("internal error");
    return r;
}

static Type* make_ptr_type(Type *ty) {
    return make_type(&(Type){ KIND_PTR, .ptr = ty, .size = 8, .align = 8 });
}

static Type* make_array_type(Type *ty, int len) {
    int size;
    if (len < 0)
        size = -1;
    else
        size = ty->size * len;
    return make_type(&(Type){
        KIND_ARRAY,
        .ptr = ty,
        .size = size,
        .len = len,
        .align = ty->align });
}

static Type* make_rectype(bool is_struct) {
    return make_type(&(Type){ KIND_STRUCT, .is_struct = is_struct });
}

static Type* make_func_type(Type *rettype, Vector *paramtypes, bool has_varargs, bool oldstyle) {
    return make_type(&(Type){
        KIND_FUNC,
        .rettype = rettype,
        .params = paramtypes,
        .hasva = has_varargs,
        .oldstyle = oldstyle });
}

static Type *make_stub_type(void) {
    return make_type(&(Type){ KIND_STUB });
}

/*
 * Predicates and kind checking routines
 */

bool is_inttype(Type *ty) {
    switch (ty->kind) {
    case KIND_BOOL: case KIND_CHAR: case KIND_SHORT: case KIND_INT:
    case KIND_LONG: case KIND_LLONG:
        return true;
    default:
        return false;
    }
}

bool is_flotype(Type *ty) {
    switch (ty->kind) {
    case KIND_FLOAT: case KIND_DOUBLE: case KIND_LDOUBLE:
        return true;
    default:
        return false;
    }
}

static bool is_arithtype(Type *ty) {
    return is_inttype(ty) || is_flotype(ty);
}

static bool is_string(Type *ty) {
    return ty->kind == KIND_ARRAY && ty->ptr->kind == KIND_CHAR;
}

static void ensure_lvalue(Node *node) {
    switch (node->kind) {
    case AST_LVAR: case AST_GVAR: case AST_DEREF: case AST_STRUCT_REF:
        return;
    default:
        error("lvalue expected, but got %s", a2s(node));
    }
}

static void ensure_inttype(Node *node) {
    if (!is_inttype(node->ty))
        error("integer kind expected, but got %s", a2s(node));
}

static void ensure_arithtype(Node *node) {
    if (!is_arithtype(node->ty))
        error("arithmetic kind expected, but got %s", a2s(node));
}

static void ensure_not_void(Type *ty) {
    if (ty->kind == KIND_VOID)
        error("void is not allowed");
}

static void expect(char id) {
    Token *tok = read_token();
    if (!is_keyword(tok, id))
        error("'%c' expected, but got %s", id, t2s(tok));
}

static Type *copy_incomplete_type(Type *ty) {
    if (!ty) return NULL;
    return (ty->len == -1) ? copy_type(ty) : ty;
}

static Type *get_typedef(char *name) {
    Node *node = map_get(env(), name);
    return (node && node->kind == AST_TYPEDEF) ? node->ty : NULL;
}

static bool is_type_keyword(Token *tok) {
    if (tok->kind == TIDENT)
        return get_typedef(tok->sval);
    if (tok->kind != TKEYWORD)
        return false;
    switch (tok->id) {
#define op(x, y)
#define keyword(id, _, istype) case id: return istype;
#include "keyword.h"
#undef keyword
#undef op
    default:
        return false;
    }
}

static bool next_token(int kind) {
    Token *tok = read_token();
    if (is_keyword(tok, kind))
        return true;
    unget_token(tok);
    return false;
}

void *make_pair(void *first, void *second) {
    void **r = malloc(sizeof(void *) * 2);
    r[0] = first;
    r[1] = second;
    return r;
}

/*
 * Type conversion
 */

static Node *conv(Node *node) {
    if (!node)
        return NULL;
    // C11 6.3.2.1p4: A function designator is converted to a pointer to the function.
    if (node->kind == AST_FUNCDESG)
        return ast_uop(AST_ADDR, make_ptr_type(node->fptr->ty), node->fptr);
    // C11 6.3.2.1p3: An array of T is converted to a pointer to T.
    Type *ty = node->ty;
    if (ty->kind == KIND_ARRAY)
        return ast_uop(AST_CONV, make_ptr_type(ty->ptr), node);
    // C11 6.3.1.1p2: The integer promotions
    switch (ty->kind) {
    case KIND_SHORT: case KIND_CHAR: case KIND_BOOL:
        return ast_conv(type_int, node);
    case KIND_INT:
        if (ty->bitsize > 0)
            return ast_conv(type_int, node);
    }
    return node;
}

static bool same_arith_type(Type *t, Type *u) {
    return t->kind == u->kind && t->usig == u->usig;
}

static Node *wrap(Type *t, Node *node) {
    if (same_arith_type(t, node->ty))
        return node;
    return ast_uop(AST_CONV, t, node);
}

// C11 6.3.1.8: Usual arithmetic conversions
static Type *usual_arith_conv(Type *t, Type *u) {
    assert(is_arithtype(t));
    assert(is_arithtype(u));
    if (t->kind < u->kind) {
        // Make t the larger type
        Type *tmp = t;
        t = u;
        u = tmp;
    }
    if (is_flotype(t))
        return t;
    assert(is_inttype(t) && t->size >= type_int->size);
    assert(is_inttype(u) && u->size >= type_int->size);
    if (t->size > u->size)
        return t;
    assert(t->size == u->size);
    if (t->usig == u->usig)
        return t;
    Type *r = copy_type(t);
    r->usig = true;
    return r;
}

static Node *binop(int op, Node *lhs, Node *rhs) {
    if (lhs->ty->kind == KIND_PTR)
        return ast_binop(lhs->ty, op, lhs, rhs);
    if (rhs->ty->kind == KIND_PTR)
        return ast_binop(rhs->ty, op, rhs, lhs);
    assert(is_arithtype(lhs->ty));
    assert(is_arithtype(rhs->ty));
    Type *r = usual_arith_conv(lhs->ty, rhs->ty);
    return ast_binop(r, op, wrap(r, lhs), wrap(r, rhs));
}

static bool is_same_struct(Type *a, Type *b) {
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
    case KIND_ARRAY:
        return a->len == b->len &&
            is_same_struct(a->ptr, b->ptr);
    case KIND_PTR:
        return is_same_struct(a->ptr, b->ptr);
    case KIND_STRUCT: {
        if (a->is_struct != b->is_struct)
            return false;
        Vector *ka = dict_keys(a->fields);
        Vector *kb = dict_keys(b->fields);
        if (vec_len(ka) != vec_len(kb))
            return false;
        for (int i = 0; i < vec_len(ka); i++)
            if (!is_same_struct(vec_get(ka, i), vec_get(kb, i)))
                return false;
        return true;
    }
    default:
        return true;
    }
}

static void ensure_assignable(Type *totype, Type *fromtype) {
    if ((is_arithtype(totype) || totype->kind == KIND_PTR) &&
        (is_arithtype(fromtype) || fromtype->kind == KIND_PTR))
        return;
    if (is_same_struct(totype, fromtype))
        return;
    error("incompatible kind: <%s> <%s>", c2s(totype), c2s(fromtype));
}

/*
 * Integer constant expression
 */

static int eval_struct_ref(Node *node, int offset) {
    if (node->kind == AST_STRUCT_REF)
        return eval_struct_ref(node->struc, node->ty->offset + offset);
    return eval_intexpr(node) + offset;
}

int eval_intexpr(Node *node) {
    switch (node->kind) {
    case AST_LITERAL:
        if (is_inttype(node->ty))
            return node->ival;
        error("Integer expression expected, but got %s", a2s(node));
    case '!': return !eval_intexpr(node->operand);
    case '~': return ~eval_intexpr(node->operand);
    case OP_UMINUS: return -eval_intexpr(node->operand);
    case OP_CAST: return eval_intexpr(node->operand);
    case AST_CONV: return eval_intexpr(node->operand);
    case AST_ADDR:
        if (node->operand->kind == AST_STRUCT_REF)
            return eval_struct_ref(node->operand, 0);
        goto error;
    case AST_DEREF:
        if (node->operand->ty->kind == KIND_PTR)
            return eval_intexpr(node->operand);
        goto error;
    case AST_TERNARY: {
        long cond = eval_intexpr(node->cond);
        if (cond)
            return node->then ? eval_intexpr(node->then) : cond;
        return eval_intexpr(node->els);
    }
#define L (eval_intexpr(node->left))
#define R (eval_intexpr(node->right))
    case '+': return L + R;
    case '-': return L - R;
    case '*': return L * R;
    case '/': return L / R;
    case '<': return L < R;
    case '^': return L ^ R;
    case '&': return L & R;
    case '|': return L | R;
    case '%': return L % R;
    case OP_EQ: return L == R;
    case OP_LE: return L <= R;
    case OP_NE: return L != R;
    case OP_SAL: return L << R;
    case OP_SAR: return L >> R;
    case OP_SHR: return ((unsigned long)L) >> R;
    case OP_LOGAND: return L && R;
    case OP_LOGOR:  return L || R;
#undef L
#undef R
    default:
    error:
        error("Integer expression expected, but got %s", a2s(node));
    }
}

static int read_intexpr() {
    return eval_intexpr(read_conditional_expr());
}

/*
 * Numeric literal
 */

#define strtoint(f, nptr, end, base)                         \
    ({                                                       \
        errno = 0;                                           \
        char *endptr;                                        \
        long r = f((nptr), &endptr, (base));                 \
        if (errno)                                           \
            error("invalid constant: %s", strerror(errno));  \
        if (endptr != (end))                                 \
            error("invalid digit '%c'", *endptr);            \
        r;                                                   \
    })

static Node *read_int(char *s) {
    char *p = s;
    int base = 10;
    if (strncasecmp(s, "0x", 2) == 0) {
        base = 16;
        p += 2;
    } else if (strncasecmp(s, "0b", 2) == 0) {
        base = 2;
        p += 2;
    } else if (s[0] == '0' && s[1] != '\0') {
        base = 8;
        p++;
    }
    char *digits = p;
    while (isxdigit(*p)) {
        if (base == 10 && isalpha(*p))
            error("invalid digit '%c' in a decimal number: %s", *p, s);
        if (base == 8 && !('0' <= *p && *p <= '7'))
            error("invalid digit '%c' in a octal number: %s", *p, s);
        if (base == 2 && (*p != '0' && *p != '1'))
            error("invalid digit '%c' in a binary number: %s", *p, s);
        p++;
    }
    if (!strcasecmp(p, "u"))
        return ast_inttype(type_uint, strtoint(strtol, s, p, base));
    if (!strcasecmp(p, "l"))
        return ast_inttype(type_long, strtoint(strtol, s, p, base));
    if (!strcasecmp(p, "ul") || !strcasecmp(p, "lu"))
        return ast_inttype(type_ulong, strtoint(strtoul, s, p, base));
    if (!strcasecmp(p, "ll"))
        return ast_inttype(type_llong, strtoint(strtol, s, p, base));
    if (!strcasecmp(p, "ull") || !strcasecmp(p, "llu"))
        return ast_inttype(type_ullong, strtoint(strtoul, s, p, base));
    if (*p != '\0')
        error("invalid suffix '%c': %s", *p, s);
    // C11 6.4.4.1p5: decimal constant type is int, long, or long long.
    // In 8cc, long and long long are the same size.
    if (base == 10) {
        long val = strtoint(strtol, digits, p, base);
        Type *t = !(val & ~(long)INT_MAX) ? type_int : type_long;
        return ast_inttype(t, val);
    }
    // Octal or hexadecimal constant type may be unsigned.
    unsigned long val = strtoint(strtoull, digits, p, base);
    Type *t = !(val & ~(unsigned long)INT_MAX) ? type_int
        : !(val & ~(unsigned long)UINT_MAX) ? type_uint
        : !(val & ~(unsigned long)LONG_MAX) ? type_long
        : type_ulong;
    return ast_inttype(t, val);
}


#define strtofloat(f, nptr, end)                                \
    ({                                                          \
        errno = 0;                                              \
        char *endptr;                                           \
        double r = f((nptr), &endptr);                          \
        if (errno)                                              \
            error("invalid constant: %s", strerror(errno));     \
        if (endptr != (end))                                    \
            error("invalid digit '%c' in %s", *endptr, nptr);   \
        r;                                                      \
    })

static Node *read_float(char *s) {
    char *last = s + strlen(s) - 1;
    // C11 6.4.4.2p4: the default type for flonum is double.
    if (strchr("lL", *last))
        return ast_floattype(type_ldouble, strtofloat(strtof, s, last));
    if (strchr("fF", *last))
        return ast_floattype(type_float, strtofloat(strtof, s, last));
    return ast_floattype(type_double, strtofloat(strtod, s, last + 1));
}

static Node *read_number(char *s) {
    bool isfloat = strpbrk(s, ".pP") || (strncasecmp(s, "0x", 2) && strpbrk(s, "eE"));
    return isfloat ? read_float(s) : read_int(s);
}

/*
 * Sizeof operator
 */

static Type *read_sizeof_operand_sub(void) {
    Token *tok = read_token();
    if (is_keyword(tok, '(') && is_type_keyword(peek_token())) {
        Type *r = read_func_param(NULL, true);
        expect(')');
        return r;
    }
    unget_token(tok);
    Node *expr = read_unary_expr();
    return (expr->kind == AST_FUNCDESG) ? expr->fptr->ty : expr->ty;
}

static Node *read_sizeof_operand(void) {
    Type *ty = read_sizeof_operand_sub();
    // Sizeof on void or function type is GNU extension
    int size = (ty->kind == KIND_VOID || ty->kind == KIND_FUNC) ? 1 : ty->size;
    assert(0 <= size);
    return ast_inttype(type_ulong, size);
}

/*
 * Alignof operator
 */

static Node *read_alignof_operand(void) {
    expect('(');
    Type *ty = read_func_param(NULL, true);
    expect(')');
    return ast_inttype(type_ulong, ty->align);
}

/*
 * Builtin functions for varargs
 */

static Node *read_va_start(void) {
    // void __builtin_va_start(va_list ap)
    Node *ap = read_assignment_expr();
    expect(')');
    return ast_va_start(ap);
}

static Node *read_va_arg(void) {
    // <type> __builtin_va_arg(va_list ap, <type>)
    Node *ap = read_assignment_expr();
    expect(',');
    Type *ty = read_cast_type();
    expect(')');
    return ast_va_arg(ty, ap);
}

/*
 * Function arguments
 */

static Vector *read_func_args(Vector *params) {
    Vector *args = make_vector();
    int i = 0;
    for (;;) {
        if (next_token(')')) break;
        Node *arg = conv(read_assignment_expr());
        Type *paramtype;
        if (i < vec_len(params)) {
            paramtype = vec_get(params, i++);
        } else {
            paramtype = is_flotype(arg->ty) ? type_double :
                is_inttype(arg->ty) ? type_int :
                arg->ty;
        }
        ensure_assignable(paramtype, arg->ty);
        if (paramtype->kind != arg->ty->kind)
            arg = ast_conv(paramtype, arg);
        vec_push(args, arg);
        Token *tok = read_token();
        if (is_keyword(tok, ')')) break;
        if (!is_keyword(tok, ','))
            error("Unexpected token: '%s'", t2s(tok));
    }
    return args;
}

static Node *read_funcall(char *fname, Node *func) {
    if (strcmp(fname, "__builtin_va_start") == 0)
        return read_va_start();
    if (strcmp(fname, "__builtin_va_arg") == 0)
        return read_va_arg();
    if (func) {
        Type *t = func->ty;
        if (t->kind != KIND_FUNC)
            error("%s is not a function, but %s", fname, c2s(t));
        Vector *args = read_func_args(t->params);
        return ast_funcall(t, fname, args);
    }
    warn("assume returning int: %s()", fname);
    Vector *args = read_func_args(&EMPTY_VECTOR);
    return ast_funcall(make_func_type(type_int, make_vector(), true, false), fname, args);
}

static Node *read_funcptr_call(Node *fptr) {
    Vector *args = read_func_args(fptr->ty->ptr->params);
    return ast_funcptr_call(fptr, args);
}

/*
 * _Generic
 */

static bool type_compatible(Type *a, Type *b) {
    if (a->kind == KIND_STRUCT)
        return is_same_struct(a, b);
    if (a->kind != b->kind)
        return false;
    if (a->ptr && b->ptr)
        return type_compatible(a->ptr, b->ptr);
    if (is_arithtype(a) && is_arithtype(b))
        return same_arith_type(a, b);
    return true;
}

static Vector *read_generic_list(Node **defaultexpr) {
    Vector *r = make_vector();
    for (;;) {
        if (next_token(')'))
            return r;
        if (next_token(KDEFAULT)) {
            if (*defaultexpr)
                error("default expression specified twice");
            expect(':');
            *defaultexpr = read_assignment_expr();
        } else {
            Type *ty = read_cast_type();
            expect(':');
            Node *expr = read_assignment_expr();
            vec_push(r, make_pair(ty, expr));
        }
        next_token(',');
    }
}

static Node *read_generic(void) {
    expect('(');
    Node *contexpr = read_assignment_expr();
    expect(',');
    Node *defaultexpr = NULL;
    Vector *list = read_generic_list(&defaultexpr);
    for (int i = 0; i < vec_len(list); i++) {
        void **pair = vec_get(list, i);
        Type *ty = pair[0];
        Node *expr = pair[1];
        if (type_compatible(contexpr->ty, ty))
            return expr;
    }
   if (!defaultexpr)
       error("no matching generic selection for %s: %s", a2s(contexpr), c2s(contexpr->ty));
   return defaultexpr;
}

/*
 * _Static_assert
 */

static void read_static_assert(void) {
    expect('(');
    int val = read_intexpr();
    expect(',');
    Token *tok = read_token();
    if (tok->kind != TSTRING)
        error("String expected as the second argument for _Static_assert, but got %s", t2s(tok));
    expect(')');
    expect(';');
    if (!val)
        error("_Static_assert failure: %s", tok->sval);
}

/*
 * Expression
 */

static Node *read_var_or_func(char *name) {
    Node *v = map_get(env(), name);
    if (!v || v->ty->kind == KIND_FUNC)
        return ast_funcdesg(name, v);
    return v;
}

static int get_compound_assign_op(Token *tok) {
    if (tok->kind != TKEYWORD)
        return 0;
    switch (tok->id) {
    case OP_A_ADD: return '+';
    case OP_A_SUB: return '-';
    case OP_A_MUL: return '*';
    case OP_A_DIV: return '/';
    case OP_A_MOD: return '%';
    case OP_A_AND: return '&';
    case OP_A_OR:  return '|';
    case OP_A_XOR: return '^';
    case OP_A_SAL: return OP_SAL;
    case OP_A_SAR: return OP_SAR;
    case OP_A_SHR: return OP_SHR;
    default: return 0;
    }
}

static Node *read_stmt_expr(void) {
    Node *r = read_compound_stmt();
    expect(')');
    Type *rtype = type_void;
    if (vec_len(r->stmts) > 0) {
        Node *lastexpr = vec_tail(r->stmts);
        if (lastexpr->ty)
            rtype = lastexpr->ty;
    }
    r->ty = rtype;
    return r;
}

static Node *read_primary_expr(void) {
    Token *tok = read_token();
    if (!tok) return NULL;
    if (is_keyword(tok, '(')) {
        if (next_token('{'))
            return read_stmt_expr();
        Node *r = read_expr();
        expect(')');
        return r;
    }
    if (is_keyword(tok, KGENERIC)) {
        return read_generic();
    }
    switch (tok->kind) {
    case TIDENT:
        return read_var_or_func(tok->sval);
    case TNUMBER:
        return read_number(tok->sval);
    case TCHAR:
        return ast_inttype(type_int, tok->c);
    case TSTRING:
        return ast_string(tok->sval);
    case TKEYWORD:
        unget_token(tok);
        return NULL;
    default:
        error("internal error: unknown token kind: %d", tok->kind);
    }
}

static Node *read_subscript_expr(Node *node) {
    Node *sub = read_expr();
    if (!sub)
        error("subscription expected");
    expect(']');
    Node *t = binop('+', conv(node), conv(sub));
    return ast_uop(AST_DEREF, t->ty->ptr, t);
}

static Node *read_postfix_expr_tail(Node *node) {
    if (!node) return NULL;
    for (;;) {
        if (next_token('(')) {
            Type *t = node->ty;
            if (t->kind == KIND_PTR && t->ptr->kind == KIND_FUNC)
                return read_funcptr_call(node);
            if (node->kind != AST_FUNCDESG)
                error("function name expected, but got %s", a2s(node));
            node = read_funcall(node->fname, node->fptr);
            continue;
        }
        if (node->kind == AST_FUNCDESG && !node->fptr)
            error("Undefined varaible: %s", node->fname);
        if (next_token('[')) {
            node = read_subscript_expr(node);
            continue;
        }
        if (next_token('.')) {
            node = read_struct_field(node);
            continue;
        }
        if (next_token(OP_ARROW)) {
            if (node->ty->kind != KIND_PTR)
                error("pointer kind expected, but got %s %s",
                      c2s(node->ty), a2s(node));
            node = ast_uop(AST_DEREF, node->ty->ptr, node);
            node = read_struct_field(node);
            continue;
        }
        Token *tok = peek_token();
        if (next_token(OP_INC) || next_token(OP_DEC)) {
            ensure_lvalue(node);
            int op = is_keyword(tok, OP_INC) ? OP_POST_INC : OP_POST_DEC;
            return ast_uop(op, node->ty, node);
        }
        return node;
    }
}

static Node *read_postfix_expr(void) {
    Node *node = read_primary_expr();
    return read_postfix_expr_tail(node);
}

static Node *read_unary_incdec(int op) {
    Node *operand = read_unary_expr();
    operand = conv(operand);
    ensure_lvalue(operand);
    return ast_uop(op, operand->ty, operand);
}

static Node *read_label_addr(void) {
    // [GNU] Labels as values. You can get the address of the a label
    // with unary "&&" operator followed by a label name.
    Token *tok = read_token();
    if (tok->kind != TIDENT)
        error("Label name expected after &&, but got %s", t2s(tok));
    Node *r = ast_label_addr(tok->sval);
    vec_push(gotos, r);
    return r;
}

static Node *read_unary_addr(void) {
    Node *operand = read_cast_expr();
    if (operand->kind == AST_FUNCDESG)
        return conv(operand);
    ensure_lvalue(operand);
    return ast_uop(AST_ADDR, make_ptr_type(operand->ty), operand);
}

static Node *read_unary_deref(void) {
    Node *operand = conv(read_cast_expr());
    if (operand->ty->kind != KIND_PTR)
        error("pointer kind expected, but got %s", a2s(operand));
    if (operand->ty->ptr->kind == KIND_FUNC)
        return operand;
    return ast_uop(AST_DEREF, operand->ty->ptr, operand);
}

static Node *read_unary_minus(void) {
    Node *expr = read_cast_expr();
    ensure_arithtype(expr);
    return ast_uop(OP_UMINUS, expr->ty, expr);
}

static Node *read_unary_bitnot(void) {
    Node *expr = read_cast_expr();
    expr = conv(expr);
    if (!is_inttype(expr->ty))
        error("invalid use of ~: %s", a2s(expr));
    return ast_uop('~', expr->ty, expr);
}

static Node *read_unary_lognot(void) {
    Node *operand = read_cast_expr();
    operand = conv(operand);
    return ast_uop('!', type_int, operand);
}

static Node *read_unary_expr(void) {
    Token *tok = read_token();
    if (tok->kind == TKEYWORD) {
        switch (tok->id) {
        case KSIZEOF: return read_sizeof_operand();
        case KALIGNOF:
        case K__ALIGNOF__: return read_alignof_operand();
        case OP_INC: return read_unary_incdec(OP_PRE_INC);
        case OP_DEC: return read_unary_incdec(OP_PRE_DEC);
        case OP_LOGAND: return read_label_addr();
        case '&': return read_unary_addr();
        case '*': return read_unary_deref();
        case '+': return read_cast_expr();
        case '-': return read_unary_minus();
        case '~': return read_unary_bitnot();
        case '!': return read_unary_lognot();
        }
    }
    unget_token(tok);
    return read_postfix_expr();
}

static Node *read_compound_literal(Type *ty) {
    char *name = make_label();
    Vector *init = read_decl_init(ty);
    Node *r = ast_lvar(ty, name);
    r->lvarinit = init;
    return r;
}

static Type *read_cast_type(void) {
    Type *basetype = read_decl_spec(NULL);
    return read_declarator(NULL, basetype, NULL, DECL_CAST);
}

static Node *read_cast_expr(void) {
    Token *tok = read_token();
    if (is_keyword(tok, '(') && is_type_keyword(peek_token())) {
        Type *ty = read_cast_type();
        expect(')');
        if (is_keyword(peek_token(), '{')) {
            Node *node = read_compound_literal(ty);
            return read_postfix_expr_tail(node);
        }
        return ast_uop(OP_CAST, ty, read_cast_expr());
    }
    unget_token(tok);
    return read_unary_expr();
}

static Node *read_multiplicative_expr(void) {
    Node *node = read_cast_expr();
    for (;;) {
        if (next_token('*'))      node = binop('*', conv(node), conv(read_cast_expr()));
        else if (next_token('/')) node = binop('/', conv(node), conv(read_cast_expr()));
        else if (next_token('%')) node = binop('%', conv(node), conv(read_cast_expr()));
        else    return node;
    }
}

static Node *read_additive_expr(void) {
    Node *node = read_multiplicative_expr();
    for (;;) {
        if      (next_token('+')) node = binop('+', conv(node), conv(read_multiplicative_expr()));
        else if (next_token('-')) node = binop('-', conv(node), conv(read_multiplicative_expr()));
        else    return node;
    }
}

static Node *read_shift_expr(void) {
    Node *node = read_additive_expr();
    for (;;) {
        int op;
        if (next_token(OP_SAL))
            op = OP_SAL;
        else if (next_token(OP_SAR))
            op = node->ty->usig ? OP_SHR : OP_SAR;
        else
            break;
        Node *right = read_additive_expr();
        ensure_inttype(node);
        ensure_inttype(right);
        node = ast_binop(node->ty, op, conv(node), conv(right));
    }
    return node;
}

static Node *read_relational_expr(void) {
    Node *node = read_shift_expr();
    for (;;) {
        if      (next_token('<'))   node = binop('<',   conv(node), conv(read_shift_expr()));
        else if (next_token('>'))   node = binop('<',   conv(read_shift_expr()), conv(node));
        else if (next_token(OP_LE)) node = binop(OP_LE, conv(node), conv(read_shift_expr()));
        else if (next_token(OP_GE)) node = binop(OP_LE, conv(read_shift_expr()), conv(node));
        else    return node;
        node->ty = type_int;
    }
}

static Node *read_equality_expr(void) {
    Node *node = read_relational_expr();
    Node *r;
    if (next_token(OP_EQ)) {
        r = binop(OP_EQ, conv(node), conv(read_equality_expr()));
    } else if (next_token(OP_NE)) {
        r = binop(OP_NE, conv(node), conv(read_equality_expr()));
    } else {
        return node;
    }
    r->ty = type_int;
    return r;
}

static Node *read_bitand_expr(void) {
    Node *node = read_equality_expr();
    while (next_token('&'))
        node = binop('&', conv(node), conv(read_equality_expr()));
    return node;
}

static Node *read_bitxor_expr(void) {
    Node *node = read_bitand_expr();
    while (next_token('^'))
        node = binop('^', conv(node), conv(read_bitand_expr()));
    return node;
}

static Node *read_bitor_expr(void) {
    Node *node = read_bitxor_expr();
    while (next_token('|'))
        node = binop('|', conv(node), conv(read_bitxor_expr()));
    return node;
}

static Node *read_logand_expr(void) {
    Node *node = read_bitor_expr();
    while (next_token(OP_LOGAND))
        node = ast_binop(type_int, OP_LOGAND, node, read_bitor_expr());
    return node;
}

static Node *read_logor_expr(void) {
    Node *node = read_logand_expr();
    while (next_token(OP_LOGOR))
        node = ast_binop(type_int, OP_LOGOR, node, read_logand_expr());
    return node;
}

static Node *do_read_conditional_expr(Node *cond) {
    Node *then = conv(read_comma_expr());
    expect(':');
    Node *els = conv(read_conditional_expr());
    // [GNU] Omitting the middle operand is allowed.
    Type *t = then ? then->ty : cond->ty;
    Type *u = els->ty;
    // C11 6.5.15p5: if both types are arithemtic type, the result
    // type is the result of the usual arithmetic conversions.
    if (is_arithtype(t) && is_arithtype(u)) {
        Type *r = usual_arith_conv(t, u);
        return ast_ternary(r, cond, (then ? wrap(r, then) : NULL), wrap(r, els));
    }
    return ast_ternary(u, cond, then, els);
}

static Node *read_conditional_expr(void) {
    Node *cond = read_logor_expr();
    if (!next_token('?'))
        return cond;
    return do_read_conditional_expr(cond);
}

static Node *read_assignment_expr(void) {
    Node *node = read_logor_expr();
    Token *tok = read_token();
    if (!tok)
        return node;
    if (is_keyword(tok, '?'))
        return do_read_conditional_expr(node);
    int cop = get_compound_assign_op(tok);
    if (is_keyword(tok, '=') || cop) {
        Node *value = conv(read_assignment_expr());
        if (is_keyword(tok, '=') || cop)
            ensure_lvalue(node);
        Node *right = cop ? binop(cop, node, value) : value;
        if (is_arithtype(node->ty) && node->ty->kind != right->ty->kind)
            right = ast_conv(node->ty, right);
        return ast_binop(node->ty, '=', node, right);
    }
    unget_token(tok);
    return node;
}

static Node *read_comma_expr(void) {
    Node *node = read_assignment_expr();
    while (next_token(',')) {
        Node *expr = read_assignment_expr();
        node = ast_binop(expr->ty, ',', node, expr);
    }
    return node;
}

Node *read_expr(void) {
    Node *r = read_comma_expr();
    if (!r)
        error("expression expected");
    return r;
}

static Node *read_expr_opt(void) {
    return read_comma_expr();
}

/*
 * Struct or union
 */

static Node *read_struct_field(Node *struc) {
    if (struc->ty->kind != KIND_STRUCT)
        error("struct expected, but got %s", a2s(struc));
    Token *name = read_token();
    if (name->kind != TIDENT)
        error("field name expected, but got %s", t2s(name));
    Type *field = dict_get(struc->ty->fields, name->sval);
    if (!field)
        error("struct has no such field: %s", t2s(name));
    return ast_struct_ref(field, struc, name->sval);
}

static char *read_rectype_tag(void) {
    Token *tok = read_token();
    if (tok->kind == TIDENT)
        return tok->sval;
    unget_token(tok);
    return NULL;
}

static int compute_padding(int offset, int align) {
    return (offset % align == 0) ? 0 : align - offset % align;
}

static void squash_unnamed_struct(Dict *dict, Type *unnamed, int offset) {
    Vector *keys = dict_keys(unnamed->fields);
    for (int i = 0; i < vec_len(keys); i++) {
        char *name = vec_get(keys, i);
        Type *t = copy_type(dict_get(unnamed->fields, name));
        t->offset += offset;
        dict_put(dict, name, t);
    }
}

static int maybe_read_bitsize(char *name, Type *ty) {
    if (!next_token(':'))
        return -1;
    if (!is_inttype(ty))
        error("non-integer kind cannot be a bitfield: %s", c2s(ty));
    int r = read_intexpr();
    int maxsize = ty->kind == KIND_BOOL ? 1 : ty->size * 8;
    if (r < 0 || maxsize < r)
        error("invalid bitfield size for %s: %d", c2s(ty), r);
    if (r == 0 && name != NULL)
        error("zero-width bitfield needs to be unnamed: %s", name);
    return r;
}

static Vector *read_rectype_fields_sub(void) {
    Vector *r = make_vector();
    for (;;) {
        if (next_token(KSTATIC_ASSERT)) {
            read_static_assert();
            continue;
        }
        if (!is_type_keyword(peek_token()))
            break;
        Type *basetype = read_decl_spec(NULL);
        if (basetype->kind == KIND_STRUCT && next_token(';')) {
            vec_push(r, make_pair(NULL, basetype));
            continue;
        }
        for (;;) {
            char *name = NULL;
            Type *fieldtype = read_declarator(&name, basetype, NULL, DECL_PARAM_TYPEONLY);
            ensure_not_void(fieldtype);
            fieldtype = copy_type(fieldtype);
            fieldtype->bitsize = maybe_read_bitsize(name, fieldtype);
            vec_push(r, make_pair(name, fieldtype));
            if (next_token(','))
                continue;
            if (is_keyword(peek_token(), '}'))
                warn("missing ';' at the end of field list");
            else
                expect(';');
            break;
        }
    }
    expect('}');
    return r;
}

static void fix_rectype_flexible_member(Vector *fields) {
    for (int i = 0; i < vec_len(fields); i++) {
        void **pair = vec_get(fields, i);
        char *name = pair[0];
        Type *ty = pair[1];
        if (ty->kind != KIND_ARRAY)
            continue;
        if (ty->len == -1) {
            if (i != vec_len(fields) - 1)
                error("flexible member may only appear as the last member: %s %s", c2s(ty), name);
            if (vec_len(fields) == 1)
                error("flexible member with no other fields: %s %s", c2s(ty), name);
            ty->len = 0;
            ty->size = 0;
        }
    }
}

static void finish_bitfield(int *off, int *bitoff) {
    *off += (*bitoff + 7) / 8;
    *bitoff = 0;
}

static Dict *update_struct_offset(Vector *fields, int *align, int *rsize) {
    int off = 0, bitoff = 0;
    Dict *r = make_dict();
    for (int i = 0; i < vec_len(fields); i++) {
        void **pair = vec_get(fields, i);
        char *name = pair[0];
        Type *fieldtype = pair[1];
        // C11 6.7.2.1p14: Each member is aligned to its natural boundary.
        // As a result the entire struct is aligned to the largest among its members.
        // Unnamed fields will never be accessed, so they shouldn't be taken into account
        // when calculating alignment.
        if (name)
            *align = MAX(*align, fieldtype->align);

        if (name == NULL && fieldtype->kind == KIND_STRUCT) {
            // C11 6.7.2.1p13: Anonymous struct
            finish_bitfield(&off, &bitoff);
            off += compute_padding(off, fieldtype->align);
            squash_unnamed_struct(r, fieldtype, off);
            off += fieldtype->size;
            continue;
        }
        if (fieldtype->bitsize == 0) {
            if (name)
                error("only unnamed bit-field is able to have zero width: %s", name);
            // C11 6.7.2.1p12: The zero-size bit-field indicates the end of the
            // current run of the bit-fields.
            finish_bitfield(&off, &bitoff);
            off += compute_padding(off, fieldtype->align);
            bitoff = 0;
            continue;
        }
        if (fieldtype->bitsize > 0) {
            int bit = fieldtype->size * 8;
            int room = bit - (off * 8 + bitoff) % bit;
            if (fieldtype->bitsize <= room) {
                fieldtype->offset = off;
                fieldtype->bitoff = bitoff;
            } else {
                finish_bitfield(&off, &bitoff);
                off += compute_padding(off, fieldtype->align);
                fieldtype->offset = off;
                fieldtype->bitoff = 0;
            }
            bitoff += fieldtype->bitsize;
        } else {
            finish_bitfield(&off, &bitoff);
            off += compute_padding(off, fieldtype->align);
            fieldtype->offset = off;
            off += fieldtype->size;
        }
        if (name)
            dict_put(r, name, fieldtype);
    }
    finish_bitfield(&off, &bitoff);
    *rsize = off + compute_padding(off, *align);
    return r;
}

static Dict *update_union_offset(Vector *fields, int *align, int *rsize) {
    int maxsize = 0;
    Dict *r = make_dict();
    for (int i = 0; i < vec_len(fields); i++) {
        void **pair = vec_get(fields, i);
        char *name = pair[0];
        Type *fieldtype = pair[1];
        maxsize = MAX(maxsize, fieldtype->size);
        *align = MAX(*align, fieldtype->align);
        if (name == NULL && fieldtype->kind == KIND_STRUCT) {
            squash_unnamed_struct(r, fieldtype, 0);
            continue;
        }
        fieldtype->offset = 0;
        if (fieldtype->bitsize >= 0)
            fieldtype->bitoff = 0;
        if (name)
            dict_put(r, name, fieldtype);
    }
    *rsize = maxsize;
    return r;
}

static Dict *read_rectype_fields(int *rsize, int *align, bool is_struct) {
    if (!next_token('{'))
        return NULL;
    Vector *fields = read_rectype_fields_sub();
    fix_rectype_flexible_member(fields);
    return is_struct
        ? update_struct_offset(fields, align, rsize)
        : update_union_offset(fields, align, rsize);
}

static Type *read_rectype_def(Map *env, bool is_struct) {
    char *tag = read_rectype_tag();
    Type *r;
    if (tag) {
        r = map_get(env, tag);
        if (!r) {
            r = make_rectype(is_struct);
            map_put(env, tag, r);
        }
    } else {
        r = make_rectype(is_struct);
    }
    int size = 0, align = 1;
    Dict *fields = read_rectype_fields(&size, &align, is_struct);
    r->align = align;
    if (fields) {
        r->fields = fields;
        r->size = size;
    }
    return r;
}

static Type *read_struct_def(void) {
    return read_rectype_def(struct_defs, true);
}

static Type *read_union_def(void) {
    return read_rectype_def(union_defs, false);
}

/*
 * Enum
 */

static Type *read_enum_def(void) {
    Token *tok = read_token();
    if (tok->kind == TIDENT)
        tok = read_token();
    if (!is_keyword(tok, '{')) {
        unget_token(tok);
        return type_int;
    }
    int val = 0;
    for (;;) {
        tok = read_token();
        if (is_keyword(tok, '}'))
            break;
        if (tok->kind != TIDENT)
            error("Identifier expected, but got %s", t2s(tok));
        char *name = tok->sval;

        if (next_token('='))
            val = read_intexpr();
        Node *constval = ast_inttype(type_int, val++);
        map_put(env(), name, constval);
        if (next_token(','))
            continue;
        if (next_token('}'))
            break;
        error("',' or '}' expected, but got %s", t2s(read_token()));
    }
    return type_int;
}

/*
 * Initializer
 */

static void assign_string(Vector *inits, Type *ty, char *p, int off) {
    if (ty->len == -1)
        ty->len = ty->size = strlen(p) + 1;
    int i = 0;
    for (; i < ty->len && *p; i++)
        vec_push(inits, ast_init(ast_inttype(type_char, *p++), type_char, off + i));
    for (; i < ty->len; i++)
        vec_push(inits, ast_init(ast_inttype(type_char, 0), type_char, off + i));
}

static bool maybe_read_brace(void) {
    return next_token('{');
}

static void maybe_skip_comma(void) {
    next_token(',');
}

static void skip_to_brace(void) {
    for (;;) {
        if (next_token('}'))
            return;
        if (next_token('.')) {
            read_token();
            expect('=');
        }
        Node *ignore = read_assignment_expr();
        if (!ignore)
            return;
        warn("excessive initializer: %s", a2s(ignore));
        maybe_skip_comma();
    }
}

static void read_initializer_elem(Vector *inits, Type *ty, int off, bool designated) {
    next_token('=');
    if (ty->kind == KIND_ARRAY || ty->kind == KIND_STRUCT) {
        read_initializer_list(inits, ty, off, designated);
    } else if (next_token('{')) {
        read_initializer_elem(inits, ty, off, true);
        expect('}');
    } else {
        Node *expr = conv(read_assignment_expr());
        ensure_assignable(ty, expr->ty);
        vec_push(inits, ast_init(expr, ty, off));
    }
}

static int comp_init(const void *p, const void *q) {
    int x = (*(Node **)p)->initoff;
    int y = (*(Node **)q)->initoff;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static void sort_inits(Vector *inits) {
    int len = vec_len(inits);
    Node **tmp = malloc(sizeof(Node *) * len);
    int i = 0;
    for (; i < vec_len(inits); i++) {
        Node *init = vec_get(inits, i);
        assert(init->kind == AST_INIT);
        tmp[i] = init;
    }
    qsort(tmp, len, sizeof(Node *), comp_init);
    vec_clear(inits);
    for (int i = 0; i < len; i++)
        vec_push(inits, tmp[i]);
}

static void read_struct_initializer_sub(Vector *inits, Type *ty, int off, bool designated) {
    bool has_brace = maybe_read_brace();
    Vector *keys = dict_keys(ty->fields);
    int i = 0;
    for (;;) {
        Token *tok = read_token();
        if (is_keyword(tok, '}')) {
            if (!has_brace)
                unget_token(tok);
            return;
        }
        char *fieldname;
        Type *fieldtype;
        if ((is_keyword(tok, '.') || is_keyword(tok, '[')) && !has_brace && !designated) {
            unget_token(tok);
            return;
        }
        if (is_keyword(tok, '.')) {
            tok = read_token();
            if (!tok || tok->kind != TIDENT)
                error("malformed desginated initializer: %s", t2s(tok));
            fieldname = tok->sval;
            fieldtype = dict_get(ty->fields, fieldname);
            if (!fieldtype)
                error("field does not exist: %s", t2s(tok));
            keys = dict_keys(ty->fields);
            i = 0;
            while (i < vec_len(keys)) {
                char *s = vec_get(keys, i++);
                if (strcmp(fieldname, s) == 0)
                    break;
            }
            designated = true;
        } else {
            unget_token(tok);
            if (i == vec_len(keys))
                break;
            fieldname = vec_get(keys, i++);
            fieldtype = dict_get(ty->fields, fieldname);
        }
        read_initializer_elem(inits, fieldtype, off + fieldtype->offset, designated);
        maybe_skip_comma();
        designated = false;
        if (!ty->is_struct)
            break;
    }
    if (has_brace)
        skip_to_brace();
}

static void read_struct_initializer(Vector *inits, Type *ty, int off, bool designated) {
    read_struct_initializer_sub(inits, ty, off, designated);
    sort_inits(inits);
}

static void read_array_initializer_sub(Vector *inits, Type *ty, int off, bool designated) {
    bool has_brace = maybe_read_brace();
    bool flexible = (ty->len <= 0);
    int elemsize = ty->ptr->size;
    int i;
    for (i = 0; flexible || i < ty->len; i++) {
        Token *tok = read_token();
        if (is_keyword(tok, '}')) {
            if (!has_brace)
                unget_token(tok);
            goto finish;
        }
        if ((is_keyword(tok, '.') || is_keyword(tok, '[')) && !has_brace && !designated) {
            unget_token(tok);
            return;
        }
        if (is_keyword(tok, '[')) {
            int idx = read_intexpr();
            if (idx < 0 || (!flexible && ty->len <= idx))
                error("array designator exceeds array bounds: %d", idx);
            i = idx;
            expect(']');
            designated = true;
        } else {
            unget_token(tok);
        }
        read_initializer_elem(inits, ty->ptr, off + elemsize * i, designated);
        maybe_skip_comma();
        designated = false;
    }
    if (has_brace)
        skip_to_brace();
 finish:
    if (ty->len < 0) {
        ty->len = i;
        ty->size = elemsize * i;
    }
}

static void read_array_initializer(Vector *inits, Type *ty, int off, bool designated) {
    read_array_initializer_sub(inits, ty, off, designated);
    sort_inits(inits);
}

static void read_initializer_list(Vector *inits, Type *ty, int off, bool designated) {
    Token *tok = read_token();
    if (is_string(ty)) {
        if (tok->kind == TSTRING) {
            assign_string(inits, ty, tok->sval, off);
            return;
        }
        if (is_keyword(tok, '{') && peek_token()->kind == TSTRING) {
            tok = read_token();
            assign_string(inits, ty, tok->sval, off);
            expect('}');
            return;
        }
    }
    unget_token(tok);
    if (ty->kind == KIND_ARRAY) {
        read_array_initializer(inits, ty, off, designated);
    } else if (ty->kind == KIND_STRUCT) {
        read_struct_initializer(inits, ty, off, designated);
    } else {
        Type *arraytype = make_array_type(ty, 1);
        read_array_initializer(inits, arraytype, off, designated);
    }
}

static Vector *read_decl_init(Type *ty) {
    Vector *r = make_vector();
    if (is_keyword(peek_token(), '{') || is_string(ty)) {
        read_initializer_list(r, ty, 0, false);
    } else {
        Node *init = conv(read_assignment_expr());
        if (is_arithtype(init->ty) && init->ty->kind != ty->kind)
            init = ast_conv(ty, init);
        vec_push(r, ast_init(init, ty, 0));
    }
    return r;
}

/*
 * Declarator
 *
 * C's syntax for declaration is not only hard to read for humans but also
 * hard to parse for hand-written parsers. Consider the following two cases:
 *
 *   A: int *x;
 *   B: int *x();
 *
 * A is of type pointer to int, but B is not a pointer type B is of type
 * function returning a pointer to an integer. The meaning of the first half
 * of the declaration ("int *" part) is different between them.
 *
 * In 8cc, delcarations are parsed by two functions: read_direct_declarator1
 * and read_direct_declarator2. The former function parses the first half of a
 * declaration, and the latter parses the (possibly nonexistent) parentheses
 * of a function or an array.
 */

static Type *read_func_param(char **name, bool optional) {
    int sclass = 0;
    Type *basetype = type_int;
    if (is_type_keyword(peek_token()))
        basetype = read_decl_spec(&sclass);
    else if (optional)
        error("kind expected, but got %s", t2s(peek_token()));
    return read_declarator(name, basetype, NULL, optional ? DECL_PARAM_TYPEONLY : DECL_PARAM);
}

static Type *read_func_param_list(Vector *paramvars, Type *rettype) {
    bool typeonly = !paramvars;
    Vector *paramtypes = make_vector();
    Token *tok = read_token();
    if (is_keyword(tok, KVOID) && next_token(')'))
        return make_func_type(rettype, paramtypes, false, false);
    if (is_keyword(tok, ')'))
        return make_func_type(rettype, paramtypes, true, false);
    unget_token(tok);
    bool oldstyle = true;
    for (;;) {
        if (next_token(KTHREEDOTS)) {
            if (vec_len(paramtypes) == 0)
                error("at least one parameter is required");
            expect(')');
            return make_func_type(rettype, paramtypes, true, oldstyle);
        }
        char *name;
        if (is_type_keyword(peek_token()))
            oldstyle = false;
        Type *ptype = read_func_param(&name, typeonly);
        ensure_not_void(ptype);
        if (ptype->kind == KIND_ARRAY)
            ptype = make_ptr_type(ptype->ptr);
        vec_push(paramtypes, ptype);
        if (!typeonly) {
            Node *node = ast_lvar(ptype, name);
            vec_push(paramvars, node);
        }
        Token *tok = read_token();
        if (is_keyword(tok, ')'))
            return make_func_type(rettype, paramtypes, false, oldstyle);
        if (!is_keyword(tok, ','))
            error("comma expected, but got %s", t2s(tok));
    }
}

static Type *read_direct_declarator2(Type *basetype, Vector *params) {
    if (next_token('[')) {
        int len;
        if (next_token(']')) {
            len = -1;
        } else {
            len = read_intexpr();
            expect(']');
        }
        Type *t = read_direct_declarator2(basetype, params);
        if (t->kind == KIND_FUNC)
            error("array of functions");
        return make_array_type(t, len);
    }
    if (next_token('(')) {
        if (basetype->kind == KIND_FUNC)
            error("function returning a function");
        if (basetype->kind == KIND_ARRAY)
            error("function returning an array");
        return read_func_param_list(params, basetype);
    }
    return basetype;
}

static void skip_type_qualifiers(void) {
    for (;;) {
        if (next_token(KCONST) || next_token(KVOLATILE) || next_token(KRESTRICT))
            continue;
        return;
    }
}

static Type *read_direct_declarator1(char **rname, Type *basetype, Vector *params, int ctx) {
    Token *tok = read_token();
    Token *next = peek_token();
    if (is_keyword(tok, '(') && !is_type_keyword(next) && !is_keyword(next, ')')) {
        Type *stub = make_stub_type();
        Type *t = read_direct_declarator1(rname, stub, params, ctx);
        expect(')');
        *stub = *read_direct_declarator2(basetype, params);
        return t;
    }
    if (is_keyword(tok, '*')) {
        skip_type_qualifiers();
        return read_direct_declarator1(rname, make_ptr_type(basetype), params, ctx);
    }
    if (tok->kind == TIDENT) {
        if (ctx == DECL_CAST)
            error("identifier is NOT expected, but got %s", t2s(tok));
        *rname = tok->sval;
        return read_direct_declarator2(basetype, params);
    }
    if (ctx == DECL_BODY || ctx == DECL_PARAM)
        error("identifier, ( or * are expected, but got %s", t2s(tok));
    unget_token(tok);
    return read_direct_declarator2(basetype, params);
}

static void fix_array_size(Type *t) {
    assert(t->kind != KIND_STUB);
    if (t->kind == KIND_ARRAY) {
        fix_array_size(t->ptr);
        t->size = t->len * t->ptr->size;
    } else if (t->kind == KIND_PTR) {
        fix_array_size(t->ptr);
    } else if (t->kind == KIND_FUNC) {
        fix_array_size(t->rettype);
    }
}

static Type *read_declarator(char **rname, Type *basetype, Vector *params, int ctx) {
    Type *t = read_direct_declarator1(rname, basetype, params, ctx);
    fix_array_size(t);
    return t;
}

/*
 * typeof()
 */

static Type *read_typeof(void) {
    expect('(');
    Type *r = is_type_keyword(peek_token())
        ? read_cast_type()
        : read_comma_expr()->ty;
    expect(')');
    return r;
}

/*
 * Declaration specifier
 */

static Type *read_decl_spec(int *rsclass) {
    int sclass = 0;
    Token *tok = peek_token();
    if (!is_type_keyword(tok))
        error("kind keyword expected, but got %s", t2s(tok));

    Type *usertype = NULL;
    enum { kvoid = 1, kbool, kchar, kint, kfloat, kdouble } kind = 0;
    enum { kshort = 1, klong, kllong } size = 0;
    enum { ksigned = 1, kunsigned } sig = 0;

    for (;;) {
#define setsclass(val)                          \
        do {                                    \
            if (sclass != 0) goto err;          \
            sclass = val;                       \
        } while (0)
#define set(var, val)                                                   \
        do {                                                            \
            if (var != 0) goto err;                                     \
            var = val;                                                  \
            if (kind == kbool && (size != 0 && sig != 0))               \
                goto err;                                               \
            if (size == kshort && (kind != 0 && kind != kint))          \
                goto err;                                               \
            if (size == klong && (kind != 0 && kind != kint && kind != kdouble)) \
                goto err;                                               \
            if (sig != 0 && (kind == kvoid || kind == kfloat || kind == kdouble)) \
                goto err;                                               \
            if (usertype && (kind != 0 || size != 0 || sig != 0))       \
                goto err;                                               \
        } while (0)

        tok = read_token();
        if (!tok)
            error("premature end of input");
        if (tok->kind == TIDENT && !usertype) {
            Type *def = get_typedef(tok->sval);
            if (def) {
                set(usertype, def);
                continue;
            }
        }
        if (tok->kind != TKEYWORD) {
            unget_token(tok);
            break;
        }
        switch (tok->id) {
        case KTYPEDEF:    setsclass(S_TYPEDEF); continue;
        case KEXTERN:     setsclass(S_EXTERN); continue;
        case KSTATIC:     setsclass(S_STATIC); continue;
        case KAUTO:       setsclass(S_AUTO); continue;
        case KREGISTER:   setsclass(S_REGISTER); continue;
        case KCONST:      continue;
        case KVOLATILE:   continue;
        case KINLINE:     continue;
        case KNORETURN:   continue;
        case KVOID:       set(kind, kvoid); continue;
        case KBOOL:       set(kind, kbool); continue;
        case KCHAR:       set(kind, kchar); continue;
        case KINT:        set(kind, kint); continue;
        case KFLOAT:      set(kind, kfloat); continue;
        case KDOUBLE:     set(kind, kdouble); continue;
        case KSIGNED:
        case K__SIGNED__: set(sig, ksigned); continue;
        case KUNSIGNED:   set(sig, kunsigned); continue;
        case KSHORT:      set(size, kshort); continue;
        case KSTRUCT:     set(usertype, read_struct_def()); continue;
        case KUNION:      set(usertype, read_union_def()); continue;
        case KENUM:       set(usertype, read_enum_def()); continue;
        case KLONG: {
            if (size == 0) set(size, klong);
            else if (size == klong) size = kllong;
            else goto err;
            continue;
        }
        case KTYPEOF: case K__TYPEOF__: {
            set(usertype, read_typeof());
            continue;
        }
        default:
            unget_token(tok);
            goto done;
        }
#undef set
#undef setsclass
    }
 done:
    if (rsclass)
        *rsclass = sclass;
    if (usertype)
        return usertype;
    switch (kind) {
    case kvoid:   return type_void;
    case kbool:   return make_numtype(KIND_BOOL, false);
    case kchar:   return make_numtype(KIND_CHAR, sig == kunsigned);
    case kfloat:  return make_numtype(KIND_FLOAT, false);
    case kdouble: return make_numtype(size == klong ? KIND_LDOUBLE : KIND_DOUBLE, false);
    default: break;
    }
    switch (size) {
    case kshort: return make_numtype(KIND_SHORT, sig == kunsigned);
    case klong:  return make_numtype(KIND_LONG, sig == kunsigned);
    case kllong: return make_numtype(KIND_LLONG, sig == kunsigned);
    default:     return make_numtype(KIND_INT, sig == kunsigned);
    }
    error("internal error: kind: %d, size: %d", kind, size);
 err:
    error("kind mismatch: %s", t2s(tok));
}

/*
 * Declaration
 */

static void read_static_local_var(Type *ty, char *name) {
    Node *var = ast_static_lvar(ty, name);
    Vector *init = NULL;
    if (next_token('=')) {
        Map *orig = localenv;
        localenv = NULL;
        init = read_decl_init(ty);
        localenv = orig;
    }
    vec_push(toplevels, ast_decl(var, init));
}

static void read_decl(Vector *block, bool isglobal) {
    int sclass;
    Type *basetype = read_decl_spec(&sclass);
    if (next_token(';'))
        return;
    for (;;) {
        char *name = NULL;
        Type *ty = read_declarator(&name, copy_incomplete_type(basetype), NULL, DECL_BODY);
        ty->isstatic = (sclass == S_STATIC);
        if (sclass == S_TYPEDEF) {
            ast_typedef(ty, name);
        } else if (ty->isstatic && !isglobal) {
            ensure_not_void(ty);
            read_static_local_var(ty, name);
        } else {
            ensure_not_void(ty);
            Node *var = (isglobal ? ast_gvar : ast_lvar)(ty, name);
            if (next_token('=')) {
                vec_push(block, ast_decl(var, read_decl_init(ty)));
            } else if (sclass != S_EXTERN && ty->kind != KIND_FUNC) {
                vec_push(block, ast_decl(var, NULL));
            }
        }
        if (next_token(';'))
            return;
        if (!next_token(','))
            error("';' or ',' are expected, but got %s", t2s(read_token()));
    }
}

/*
 * K&R-style parameter types
 */

static Vector *read_oldstyle_param_args(void) {
    Map *orig = localenv;
    localenv = NULL;
    Vector *r = make_vector();
    for (;;) {
        if (is_keyword(peek_token(), '{'))
            break;
        if (!is_type_keyword(peek_token()))
            error("K&R-style declarator expected, but got %s", t2s(peek_token()));
        read_decl(r, false);
    }
    localenv = orig;
    return r;
}

static void update_oldstyle_param_type(Vector *params, Vector *vars) {
    for (int i = 0; i < vec_len(vars); i++) {
        Node *decl = vec_get(vars, i);
        assert(decl->kind == AST_DECL);
        Node *var = decl->declvar;
        assert(var->kind == AST_LVAR);
        for (int j = 0; j < vec_len(params); j++) {
            Node *param = vec_get(params, j);
            assert(param->kind == AST_LVAR);
            if (strcmp(param->varname, var->varname))
                continue;
            param->ty = var->ty;
            goto found;
        }
        error("missing parameter: %s", var->varname);
    found:;
    }
}

static void read_oldstyle_param_type(Vector *params) {
    Vector *vars = read_oldstyle_param_args();
    update_oldstyle_param_type(params, vars);
}

static Vector *param_types(Vector *params) {
    Vector *r = make_vector();
    for (int i = 0; i < vec_len(params); i++) {
        Node *param = vec_get(params, i);
        vec_push(r, param->ty);
    }
    return r;
}

/*
 * Function definition
 */

static Node *read_func_body(Type *functype, char *fname, Vector *params) {
    localenv = make_map_parent(localenv);
    localvars = make_vector();
    current_func_type = functype;
    Node *funcname = ast_string(fname);
    map_put(localenv, "__func__", funcname);
    map_put(localenv, "__FUNCTION__", funcname);
    Node *body = read_compound_stmt();
    Node *r = ast_func(functype, fname, params, body, localvars);
    current_func_type = NULL;
    localenv = NULL;
    localvars = NULL;
    return r;
}

static bool is_funcdef(void) {
    Vector *buf = make_vector();
    int nest = 0;
    bool paren = false;
    bool r = true;
    for (;;) {
        Token *tok = read_token();
        vec_push(buf, tok);
        if (!tok)
            error("premature end of input");
        if (nest == 0 && paren && (is_keyword(tok, '{') || tok->kind == TIDENT))
            break;
        if (nest == 0 && (is_keyword(tok, ';') || is_keyword(tok, ',') || is_keyword(tok, '='))) {
            r = false;
            break;
        }
        if (is_keyword(tok, '(')) {
            nest++;
        }
        if (is_keyword(tok, ')')) {
            if (nest == 0)
                error("extra close parenthesis");
            paren = true;
            nest--;
        }
    }
    while (vec_len(buf) > 0)
        unget_token(vec_pop(buf));
    return r;
}

static void backfill_labels(void) {
    for (int i = 0; i < vec_len(gotos); i++) {
        Node *src = vec_get(gotos, i);
        char *label = src->label;
        Node *dst = map_get(labels, label);
        if (!dst)
            error("stray %s: %s", src->kind == AST_GOTO ? "goto" : "unary &&", label);
        if (dst->newlabel)
            src->newlabel = dst->newlabel;
        else
            src->newlabel = dst->newlabel = make_label();
    }
}

static Node *read_funcdef(void) {
    int sclass = 0;
    Type *basetype = type_int;
    if (is_type_keyword(peek_token()))
        basetype = read_decl_spec(&sclass);
    else
        warn("kind specifier missing, assuming int");
    localenv = make_map_parent(globalenv);
    gotos = make_vector();
    labels = make_map();
    char *name;
    Vector *params = make_vector();
    Type *functype = read_declarator(&name, basetype, params, DECL_BODY);
    if (functype->oldstyle) {
        read_oldstyle_param_type(params);
        functype->params = param_types(params);
    }
    functype->isstatic = (sclass == S_STATIC);
    ast_gvar(functype, name);
    expect('{');
    Node *r = read_func_body(functype, name, params);
    backfill_labels();
    localenv = NULL;
    return r;
}

/*
 * If
 */

static Node *read_boolean_expr(void) {
    Node *cond = read_expr();
    return is_flotype(cond->ty) ? ast_conv(type_bool, cond) : cond;
}

static Node *read_if_stmt(void) {
    expect('(');
    Node *cond = read_boolean_expr();
    expect(')');
    Node *then = read_stmt();
    if (!next_token(KELSE))
        return ast_if(cond, then, NULL);
    Node *els = read_stmt();
    return ast_if(cond, then, els);
}

/*
 * For
 */

static Node *read_opt_decl_or_stmt(void) {
    if (next_token(';'))
        return NULL;
    Vector *list = make_vector();
    read_decl_or_stmt(list);
    return ast_compound_stmt(list);
}

static Node *read_for_stmt(void) {
    expect('(');
    Map *orig = localenv;
    localenv = make_map_parent(localenv);
    Node *init = read_opt_decl_or_stmt();
    Node *cond = read_expr_opt();
    if (cond && is_flotype(cond->ty))
        cond = ast_conv(type_bool, cond);
    expect(';');
    Node *step = read_expr_opt();
    expect(')');
    Node *body = read_stmt();
    localenv = orig;
    return ast_for(init, cond, step, body);
}

/*
 * While
 */

static Node *read_while_stmt(void) {
    expect('(');
    Node *cond = read_boolean_expr();
    expect(')');
    Node *body = read_stmt();
    return ast_while(cond, body);
}

/*
 * Do
 */

static Node *read_do_stmt(void) {
    Node *body = read_stmt();
    Token *tok = read_token();
    if (!is_keyword(tok, KWHILE))
        error("'while' is expected, but got %s", t2s(tok));
    expect('(');
    Node *cond = read_boolean_expr();
    expect(')');
    expect(';');
    return ast_do(cond, body);
}

/*
 * Switch
 */

static Node *read_switch_stmt(void) {
    expect('(');
    Node *expr = read_expr();
    ensure_inttype(expr);
    expect(')');
    Node *body = read_stmt();
    return ast_switch(expr, body);
}

static Node *read_case_label(void) {
    int beg = read_intexpr();
    int end = next_token(KTHREEDOTS) ? read_intexpr() : beg;
    expect(':');
    if (beg > end)
        error("case region is not in correct order: %d %d", beg, end);
    return ast_case(beg, end);
}

static Node *read_default_label(void) {
    expect(':');
    return make_ast(&(Node){ AST_DEFAULT });
}

/*
 * Jump statements
 */

static Node *read_break_stmt(void) {
    expect(';');
    return make_ast(&(Node){ AST_BREAK });
}

static Node *read_continue_stmt(void) {
    expect(';');
    return make_ast(&(Node){ AST_CONTINUE });
}

static Node *read_return_stmt(void) {
    Node *retval = read_expr_opt();
    expect(';');
    if (retval)
        return ast_return(ast_conv(current_func_type->rettype, retval));
    return ast_return(NULL);
}

static Node *read_goto_stmt(void) {
    if (next_token('*')) {
        // [GNU] computed goto. "goto *p" jumps to the address pointed by p.
        Node *expr = read_cast_expr();
        if (expr->ty->kind != KIND_PTR)
            error("pointer expected for computed goto, but got %s", a2s(expr));
        return ast_computed_goto(expr);
    }
    Token *tok = read_token();
    if (!tok || tok->kind != TIDENT)
        error("identifier expected, but got %s", t2s(tok));
    expect(';');
    Node *r = ast_goto(tok->sval);
    vec_push(gotos, r);
    return r;
}

static Node *read_label(Token *tok) {
    expect(':');
    char *label = tok->sval;
    Node *r = ast_label(label);
    if (map_get(labels, label))
        error("duplicate label: %s", t2s(tok));
    map_put(labels, label, r);
    return r;
}

/*
 * Statement
 */

static Node *read_stmt(void) {
    Token *tok = read_token();
    if (tok->kind == TKEYWORD) {
        switch (tok->id) {
        case '{':       return read_compound_stmt();
        case KIF:       return read_if_stmt();
        case KFOR:      return read_for_stmt();
        case KWHILE:    return read_while_stmt();
        case KDO:       return read_do_stmt();
        case KRETURN:   return read_return_stmt();
        case KSWITCH:   return read_switch_stmt();
        case KCASE:     return read_case_label();
        case KDEFAULT:  return read_default_label();
        case KBREAK:    return read_break_stmt();
        case KCONTINUE: return read_continue_stmt();
        case KGOTO:     return read_goto_stmt();
        }
    }
    if (tok->kind == TIDENT && is_keyword(peek_token(), ':'))
        return read_label(tok);
    unget_token(tok);
    Node *r = read_expr_opt();
    expect(';');
    return r;
}

static Node *read_compound_stmt(void) {
    Map *orig = localenv;
    localenv = make_map_parent(localenv);
    Vector *list = make_vector();
    for (;;) {
        if (next_token('}'))
            break;
        read_decl_or_stmt(list);
    }
    localenv = orig;
    return ast_compound_stmt(list);
}

static void read_decl_or_stmt(Vector *list) {
    Token *tok = peek_token();
    mark_location();
    if (tok == NULL)
        error("premature end of input");
    if (is_type_keyword(tok)) {
        read_decl(list, false);
    } else if (next_token(KSTATIC_ASSERT)) {
        read_static_assert();
    } else {
        Node *stmt = read_stmt();
        if (stmt)
            vec_push(list, stmt);
    }
}

/*
 * Compilation unit
 */

Vector *read_toplevels(void) {
    toplevels = make_vector();
    for (;;) {
        if (!peek_token())
            return toplevels;
        if (is_funcdef())
            vec_push(toplevels, read_funcdef());
        else
            read_decl(toplevels, true);
    }
}

/*
 * Initializer
 */

static void define_builtin(char *name, Type *rettype, Vector *paramtypes) {
    Node *v = ast_gvar(make_func_type(rettype, paramtypes, true, false), name);
    map_put(globalenv, name, v);
}

void parse_init(void) {
    define_builtin("__builtin_va_start", type_void, &EMPTY_VECTOR);
    define_builtin("__builtin_va_arg", type_void, &EMPTY_VECTOR);
    define_builtin("__builtin_return_address", make_ptr_type(type_void), make_vector1(type_uint));
}
