// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

/*
 * Recursive descendent parser for C.
 */

#include <ctype.h>
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "8cc.h"

#define MAX_OP_PRIO 16
#define MAX_ALIGN 16

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static Dict *globalenv = &EMPTY_DICT;
static Dict *localenv;
static Dict *struct_defs = &EMPTY_DICT;
static Dict *union_defs = &EMPTY_DICT;
static List *gotos;
static Dict *labels;
static List *localvars;
static Ctype *current_func_type;

Ctype *ctype_void = &(Ctype){ CTYPE_VOID, 0, true };
Ctype *ctype_bool = &(Ctype){ CTYPE_BOOL, 1, false };
Ctype *ctype_char = &(Ctype){ CTYPE_CHAR, 1, true };
Ctype *ctype_short = &(Ctype){ CTYPE_SHORT, 2, true };
Ctype *ctype_int = &(Ctype){ CTYPE_INT, 4, true };
Ctype *ctype_long = &(Ctype){ CTYPE_LONG, 8, true };
Ctype *ctype_float = &(Ctype){ CTYPE_FLOAT, 4, true };
Ctype *ctype_double = &(Ctype){ CTYPE_DOUBLE, 8, true };
Ctype *ctype_ldouble = &(Ctype){ CTYPE_LDOUBLE, 16, true };
static Ctype *ctype_uint = &(Ctype){ CTYPE_INT, 4, false };
static Ctype *ctype_ulong = &(Ctype){ CTYPE_LONG, 8, false };
static Ctype *ctype_llong = &(Ctype){ CTYPE_LLONG, 8, true };
static Ctype *ctype_ullong = &(Ctype){ CTYPE_LLONG, 8, false };

static int labelseq = 0;

typedef Node *MakeVarFn(Ctype *ctype, char *name);

static Ctype* make_ptr_type(Ctype *ctype);
static Ctype* make_array_type(Ctype *ctype, int size);
static Node *read_compound_stmt(void);
static void read_decl_or_stmt(List *list);
static Ctype *convert_array(Ctype *ctype);
static Node *read_stmt(void);
static bool is_type_keyword(Token *tok);
static Node *read_unary_expr(void);
static Ctype *read_func_param(char **name, bool optional);
static void read_decl(List *toplevel, MakeVarFn *make_var);
static Ctype *read_declarator(char **name, Ctype *basetype, List *params, int ctx);
static Ctype *read_decl_spec(int *sclass);
static Node *read_struct_field(Node *struc);
static void read_initializer_list(List *inits, Ctype *ctype, int off);
static Ctype *read_cast_type(void);
static List *read_decl_init(Ctype *ctype);
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

/*----------------------------------------------------------------------
 * Constructors
 */

char *make_label(void) {
    return format(".L%d", labelseq++);
}

static Node *make_ast(Node *tmpl) {
    Node *r = malloc(sizeof(Node));
    *r = *tmpl;
    return r;
}

static Node *ast_uop(int type, Ctype *ctype, Node *operand) {
    return make_ast(&(Node){ type, ctype, .operand = operand });
}

static Node *ast_binop(Ctype *ctype, int type, Node *left, Node *right) {
    Node *r = make_ast(&(Node){ type, ctype });
    r->left = left;
    r->right = right;
    return r;
}

static Node *ast_inttype(Ctype *ctype, long val) {
    return make_ast(&(Node){ AST_LITERAL, ctype, .ival = val });
}

static Node *ast_floattype(Ctype *ctype, double val) {
    return make_ast(&(Node){ AST_LITERAL, ctype, .fval = val });
}

static Node *ast_lvar(Ctype *ctype, char *name) {
    Node *r = make_ast(&(Node){ AST_LVAR, ctype, .varname = name });
    if (localenv)
        dict_put(localenv, name, r);
    if (localvars)
        list_push(localvars, r);
    return r;
}

static Node *ast_gvar(Ctype *ctype, char *name) {
    Node *r = make_ast(&(Node){ AST_GVAR, ctype, .varname = name, .glabel = name });
    dict_put(globalenv, name, r);
    return r;
}

static Node *ast_typedef(Ctype *ctype, char *name) {
    Node *r = make_ast(&(Node){ AST_TYPEDEF, ctype, .typedefname = name });
    dict_put(localenv ? localenv : globalenv, name, r);
    return r;
}

static Node *ast_string(char *str) {
    return make_ast(&(Node){
        .type = AST_STRING,
        .ctype = make_array_type(ctype_char, strlen(str) + 1),
        .sval = str });
}

static Node *ast_funcall(Ctype *ftype, char *fname, List *args) {
    return make_ast(&(Node){
        .type = AST_FUNCALL,
        .ctype = ftype->rettype,
        .fname = fname,
        .args = args,
        .ftype = ftype });
}

static Node *ast_funcdesg(char *fname, Node *func) {
    return make_ast(&(Node){
        .type = AST_FUNCDESG,
        .ctype = ctype_void,
        .fname = fname,
        .fptr = func });
}

static Node *ast_funcptr_call(Node *fptr, List *args) {
    assert(fptr->ctype->type == CTYPE_PTR);
    assert(fptr->ctype->ptr->type == CTYPE_FUNC);
    return make_ast(&(Node){
        .type = AST_FUNCPTR_CALL,
        .ctype = fptr->ctype->ptr->rettype,
        .fptr = fptr,
        .args = args });
}

static Node *ast_func(Ctype *ctype, char *fname, List *params, Node *body, List *localvars) {
    return make_ast(&(Node){
        .type = AST_FUNC,
        .ctype = ctype,
        .fname = fname,
        .params = params,
        .localvars = localvars,
        .body = body});
}

static Node *ast_decl(Node *var, List *init) {
    return make_ast(&(Node){ AST_DECL, .declvar = var, .declinit = init });
}

static Node *ast_init(Node *val, Ctype *totype, int off) {
    return make_ast(&(Node){ AST_INIT, .initval = val, .initoff = off, .totype = totype });
}

static Node *ast_conv(Ctype *totype, Node *val) {
    return make_ast(&(Node){ AST_CONV, totype, .operand = val });
}

static Node *ast_if(Node *cond, Node *then, Node *els) {
    return make_ast(&(Node){ AST_IF, .cond = cond, .then = then, .els = els });
}

static Node *ast_ternary(Ctype *ctype, Node *cond, Node *then, Node *els) {
    return make_ast(&(Node){ AST_TERNARY, ctype, .cond = cond, .then = then, .els = els });
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

static Node *ast_compound_stmt(List *stmts) {
    return make_ast(&(Node){ AST_COMPOUND_STMT, .stmts = stmts });
}

static Node *ast_struct_ref(Ctype *ctype, Node *struc, char *name) {
    return make_ast(&(Node){ AST_STRUCT_REF, ctype, .struc = struc, .field = name });
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
    return make_ast(&(Node){ AST_VA_START, ctype_void, .ap = ap });
}

static Node *ast_va_arg(Ctype *ctype, Node *ap) {
    return make_ast(&(Node){ AST_VA_ARG, ctype, .ap = ap });
}

static Node *ast_label_addr(char *label) {
    return make_ast(&(Node){ OP_LABEL_ADDR, make_ptr_type(ctype_void), .label = label });
}

static Ctype *make_type(Ctype *tmpl) {
    Ctype *r = malloc(sizeof(Ctype));
    *r = *tmpl;
    return r;
}

static Ctype *copy_type(Ctype *ctype) {
    Ctype *r = malloc(sizeof(Ctype));
    memcpy(r, ctype, sizeof(Ctype));
    return r;
}

static Ctype *make_numtype(int type, bool sig) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = type;
    r->sig = sig;
    if (type == CTYPE_VOID)         r->size = 0;
    else if (type == CTYPE_BOOL)    r->size = 1;
    else if (type == CTYPE_CHAR)    r->size = 1;
    else if (type == CTYPE_SHORT)   r->size = 2;
    else if (type == CTYPE_INT)     r->size = 4;
    else if (type == CTYPE_LONG)    r->size = 8;
    else if (type == CTYPE_LLONG)   r->size = 8;
    else if (type == CTYPE_FLOAT)   r->size = 4;
    else if (type == CTYPE_DOUBLE)  r->size = 8;
    else if (type == CTYPE_LDOUBLE) r->size = 16;
    else error("internal error");
    return r;
}

static Ctype* make_ptr_type(Ctype *ctype) {
    return make_type(&(Ctype){ CTYPE_PTR, .ptr = ctype, .size = 8 });
}

static Ctype* make_array_type(Ctype *ctype, int len) {
    int size;
    if (len < 0)
        size = -1;
    else
        size = ctype->size * len;
    return make_type(&(Ctype){
        CTYPE_ARRAY,
        .ptr = ctype,
        .size = size,
        .len = len });
}

static Ctype* make_struct_type(Dict *fields, int size, bool is_struct) {
    return make_type(&(Ctype){
            CTYPE_STRUCT, .fields = fields, .size = size, .is_struct = is_struct });
}

static Ctype* make_func_type(Ctype *rettype, List *paramtypes, bool has_varargs) {
    return make_type(&(Ctype){
        CTYPE_FUNC,
        .rettype = rettype,
        .params = paramtypes,
        .hasva = has_varargs });
}

static Ctype *make_stub_type(void) {
    return make_type(&(Ctype){ CTYPE_STUB });
}

/*----------------------------------------------------------------------
 * Predicates and type checking routines
 */

bool is_inttype(Ctype *ctype) {
    switch (ctype->type) {
    case CTYPE_BOOL: case CTYPE_CHAR: case CTYPE_SHORT: case CTYPE_INT:
    case CTYPE_LONG: case CTYPE_LLONG:
        return true;
    default:
        return false;
    }
}

bool is_flotype(Ctype *ctype) {
    switch (ctype->type) {
    case CTYPE_FLOAT: case CTYPE_DOUBLE: case CTYPE_LDOUBLE:
        return true;
    default:
        return false;
    }
}

static bool is_arithtype(Ctype *ctype) {
    return is_inttype(ctype) || is_flotype(ctype);
}

static bool is_string(Ctype *ctype) {
    return ctype->type == CTYPE_ARRAY && ctype->ptr->type == CTYPE_CHAR;
}

static void ensure_lvalue(Node *node) {
    switch (node->type) {
    case AST_LVAR: case AST_GVAR: case AST_DEREF: case AST_STRUCT_REF:
        return;
    default:
        error("lvalue expected, but got %s", a2s(node));
    }
}

static void ensure_inttype(Node *node) {
    if (!is_inttype(node->ctype))
        error("integer type expected, but got %s", a2s(node));
}

static void ensure_arithtype(Node *node) {
    if (!is_arithtype(node->ctype))
        error("arithmetic type expected, but got %s", a2s(node));
}

static void ensure_not_void(Ctype *ctype) {
    if (ctype->type == CTYPE_VOID)
        error("void is not allowed");
}

static void expect(char punct) {
    Token *tok = read_token();
    if (!is_punct(tok, punct))
        error("'%c' expected, but got %s", punct, t2s(tok));
}

static Ctype *copy_incomplete_type(Ctype *ctype) {
    if (!ctype) return NULL;
    return (ctype->len == -1) ? copy_type(ctype) : ctype;
}

static Ctype *get_typedef(char *name) {
    Node *node = dict_get(localenv ? localenv : globalenv, name);
    return (node && node->type == AST_TYPEDEF) ? node->ctype : NULL;
}

static bool is_type_keyword(Token *tok) {
    if (tok->type == TIDENT)
        return get_typedef(tok->sval);
    if (tok->type != TPUNCT)
        return false;
    switch (tok->punct) {
#define punct(x, y)
#define keyword(ident, _, istype)               \
        case ident: return istype;
#include "keyword.h"
#undef keyword
#undef punct
    default:
        return false;
    }
}

static bool next_token(int type) {
    Token *tok = read_token();
    if (is_punct(tok, type))
        return true;
    unget_token(tok);
    return false;
}

void *make_pair(void *first, void *second) {
    Pair *r = malloc(sizeof(Pair));
    r->first = first;
    r->second = second;
    return r;
}

/*----------------------------------------------------------------------
 * Type conversion
 */

static int conversion_rank(Ctype *ctype) {
    assert(is_arithtype(ctype));
    switch (ctype->type) {
    case CTYPE_LDOUBLE: return 8;
    case CTYPE_DOUBLE:  return 7;
    case CTYPE_FLOAT:   return 6;
    case CTYPE_LLONG:   return 5;
    case CTYPE_LONG:    return 4;
    case CTYPE_INT:     return 3;
    case CTYPE_SHORT:   return 2;
    case CTYPE_CHAR:    return 1;
    case CTYPE_BOOL:    return 0;
    default:
        error("internal error: %s", c2s(ctype));
    }
}

static Ctype *larger_type(Ctype *a, Ctype *b) {
    return conversion_rank(a) < conversion_rank(b) ? b : a;
}

static Ctype *result_type(int op, Ctype *ctype) {
    switch (op) {
    case OP_LE: case OP_GE: case OP_EQ: case OP_NE: case '<': case '>':
        return ctype_int;
    default:
        return larger_type(ctype, ctype);
    }
}

static Node *usual_conv(int op, Node *left, Node *right) {
    if (!is_arithtype(left->ctype) || !is_arithtype(right->ctype)) {
        Ctype *resulttype;
        switch (op) {
        case OP_LE: case OP_GE: case OP_EQ: case OP_NE: case '<': case '>':
            resulttype = ctype_int;
            break;
        default:
            resulttype = convert_array(left->ctype);
            break;
        }
        return ast_binop(resulttype, op, left, right);
    }
    int rank1 = conversion_rank(left->ctype);
    int rank2 = conversion_rank(right->ctype);
    if (rank1 < rank2)
        left = ast_conv(right->ctype, left);
    else if (rank1 != rank2)
        right = ast_conv(left->ctype, right);
    Ctype *resulttype = result_type(op, left->ctype);
    return ast_binop(resulttype, op, left, right);
}

static bool is_same_struct(Ctype *a, Ctype *b) {
    if (a->type != b->type)
        return false;
    switch (a->type) {
    case CTYPE_ARRAY:
        return a->len == b->len &&
            is_same_struct(a->ptr, b->ptr);
    case CTYPE_PTR:
        return is_same_struct(a->ptr, b->ptr);
    case CTYPE_STRUCT: {
        if (a->is_struct != b->is_struct)
            return false;
        List *ka = dict_keys(a->fields);
        List *kb = dict_keys(b->fields);
        if (list_len(ka) != list_len(kb))
            return false;
        Iter *ia = list_iter(ka);
        Iter *ib = list_iter(kb);
        while (!iter_end(ia)) {
            if (!is_same_struct(iter_next(ia), iter_next(ib)))
                return false;
        }
        return true;
    }
    default:
        return true;
    }
}

static void ensure_assignable(Ctype *totype, Ctype *fromtype) {
    fromtype = convert_array(fromtype);
    if ((is_arithtype(totype) || totype->type == CTYPE_PTR) &&
        (is_arithtype(fromtype) || fromtype->type == CTYPE_PTR))
        return;
    if (is_same_struct(totype, fromtype))
        return;
    error("incompatible type: <%s> <%s>", c2s(totype), c2s(fromtype));
}

static Ctype *convert_array(Ctype *ctype) {
    if (ctype->type != CTYPE_ARRAY)
        return ctype;
    return make_ptr_type(ctype->ptr);
}

/*----------------------------------------------------------------------
 * Integer constant expression
 */

int eval_intexpr(Node *node) {
    switch (node->type) {
    case AST_LITERAL:
        if (is_inttype(node->ctype))
            return node->ival;
        error("Integer expression expected, but got %s", a2s(node));
    case '!': return !eval_intexpr(node->operand);
    case '~': return ~eval_intexpr(node->operand);
    case OP_UMINUS: return -eval_intexpr(node->operand);
    case OP_CAST: return eval_intexpr(node->operand);
    case AST_CONV: return eval_intexpr(node->operand);
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
    case '>': return L > R;
    case '^': return L ^ R;
    case '&': return L & R;
    case '%': return L % R;
    case OP_EQ: return L == R;
    case OP_GE: return L >= R;
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
        error("Integer expression expected, but got %s", a2s(node));
    }
}

static int read_intexpr() {
    return eval_intexpr(read_conditional_expr());
}

/*----------------------------------------------------------------------
 * Numeric literal
 */

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
        return ast_inttype(ctype_uint, strtol(s, NULL, base));
    if (!strcasecmp(p, "l"))
        return ast_inttype(ctype_long, strtol(s, NULL, base));
    if (!strcasecmp(p, "ul") || !strcasecmp(p, "lu"))
        return ast_inttype(ctype_ulong, strtoul(s, NULL, base));
    if (!strcasecmp(p, "ll"))
        return ast_inttype(ctype_llong, strtoll(s, NULL, base));
    if (!strcasecmp(p, "ull") || !strcasecmp(p, "llu"))
        return ast_inttype(ctype_ullong, strtoull(s, NULL, base));
    if (*p != '\0')
        error("invalid suffix '%c': %s", *p, s);
    long val = strtol(digits, NULL, base);
    return (val & ~(long)UINT_MAX)
        ? ast_inttype(ctype_long, val)
        : ast_inttype(ctype_int, val);
}

static Node *read_float(char *s) {
    char *p = s;
    char *endptr;
    while (p[1]) p++;
    Node *r;
    if (*p == 'l' || *p == 'L') {
        r = ast_floattype(ctype_ldouble, strtold(s, &endptr));
    } else if (*p == 'f' || *p == 'F') {
        r = ast_floattype(ctype_float, strtof(s, &endptr));
    } else {
        r = ast_floattype(ctype_double, strtod(s, &endptr));
        p++;
    }
    if (endptr != p)
        error("malformed floating constant: %s", s);
    return r;
}

static Node *read_number(char *s) {
    bool isfloat = strpbrk(s, ".pe");
    return isfloat ? read_float(s) : read_int(s);
}

/*----------------------------------------------------------------------
 * Sizeof operator
 */

static Ctype *read_sizeof_operand_sub(bool allow_typename) {
    Token *tok = read_token();
    if (allow_typename && is_type_keyword(tok)) {
        unget_token(tok);
        return read_func_param(NULL, true);
    }
    if (is_punct(tok, '(')) {
        Ctype *r = read_sizeof_operand_sub(true);
        if (!is_punct(peek_token(), ')'))
            read_cast_expr();
        expect(')');
        if (is_punct(peek_token(), '{'))
            read_decl_init(r);
        return r;
    }
    unget_token(tok);
    Node *expr = read_unary_expr();
    return (expr->type == AST_FUNCDESG) ? expr->fptr->ctype : expr->ctype;
}

static Node *read_sizeof_operand(void) {
    Ctype *ctype = read_sizeof_operand_sub(false);
    // Sizeof on void or function type is GNU extension
    int size = (ctype->type == CTYPE_VOID || ctype->type == CTYPE_FUNC) ? 1 : ctype->size;
    assert(0 <= size);
    return ast_inttype(ctype_ulong, size);
}

/*----------------------------------------------------------------------
 * Alignof operator
 */

static int get_alignment(Ctype *ctype) {
    int size = ctype->type == CTYPE_ARRAY ? ctype->ptr->size : ctype->size;
    return MIN(size, MAX_ALIGN);
}

static Node *read_alignof_operand(void) {
    expect('(');
    Ctype *ctype = read_func_param(NULL, true);
    expect(')');
    return ast_inttype(ctype_long, get_alignment(ctype));
}

/*----------------------------------------------------------------------
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
    Ctype *ctype = read_cast_type();
    expect(')');
    return ast_va_arg(ctype, ap);
}

/*----------------------------------------------------------------------
 * Function arguments
 */

static List *read_func_args(List *params) {
    List *args = make_list();
    Iter *iter = list_iter(params);
    for (;;) {
        if (next_token(')')) break;
        Node *arg = read_assignment_expr();
        Ctype *paramtype = iter_next(iter);
        if (!paramtype) {
            paramtype = is_flotype(arg->ctype) ? ctype_double :
                is_inttype(arg->ctype) ? ctype_int :
                arg->ctype;
        }
        ensure_assignable(convert_array(paramtype), convert_array(arg->ctype));
        if (paramtype->type != arg->ctype->type)
            arg = ast_conv(paramtype, arg);
        list_push(args, arg);
        Token *tok = read_token();
        if (is_punct(tok, ')')) break;
        if (!is_punct(tok, ','))
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
        Ctype *t = func->ctype;
        if (t->type != CTYPE_FUNC)
            error("%s is not a function, but %s", fname, c2s(t));
        List *args = read_func_args(t->params);
        return ast_funcall(t, fname, args);
    }
    warn("assume returning int: %s()", fname);
    List *args = read_func_args(&EMPTY_LIST);
    return ast_funcall(make_func_type(ctype_int, make_list(), true), fname, args);
}

static Node *read_funcptr_call(Node *fptr) {
    List *args = read_func_args(fptr->ctype->ptr->params);
    return ast_funcptr_call(fptr, args);
}

/*----------------------------------------------------------------------
 * _Generic
 */

static bool type_compatible(Ctype *a, Ctype *b) {
    if (a->type == CTYPE_STRUCT)
        return is_same_struct(a, b);
    if (a->type != b->type)
        return false;
    if (a->ptr && b->ptr)
        return type_compatible(a->ptr, b->ptr);
    return true;
}

static List *read_generic_list(Node **defaultexpr) {
    List *r = make_list();
    for (;;) {
        if (next_token(')'))
            return r;
        if (next_token(KDEFAULT)) {
            if (*defaultexpr)
                error("default expression specified twice");
            expect(':');
            *defaultexpr = read_assignment_expr();
        } else {
            Ctype *ctype = read_cast_type();
            expect(':');
            Node *expr = read_assignment_expr();
            list_push(r, make_pair(ctype, expr));
        }
        next_token(',');
    }
}

static Node *read_generic(void) {
    expect('(');
    Node *contexpr = read_assignment_expr();
    Ctype *conttype = convert_array(contexpr->ctype);
    expect(',');
    Node *defaultexpr = NULL;
    List *list = read_generic_list(&defaultexpr);
    Iter *iter = list_iter(list);
    while (!iter_end(iter)) {
        Pair *pair = iter_next(iter);
        Ctype *ctype = pair->first;
        Node *expr = pair->second;
        if (type_compatible(conttype, ctype))
            return expr;
    }
   if (!defaultexpr)
       error("no matching generic selection for %s", a2s(contexpr));
   return defaultexpr;
}

/*----------------------------------------------------------------------
 * _Static_assert
 */

static void read_static_assert(void) {
    expect('(');
    int val = read_intexpr();
    expect(',');
    Token *tok = read_token();
    if (tok->type != TSTRING)
        error("String expected as the second argument for _Static_assert, but got %s", t2s(tok));
    expect(')');
    expect(';');
    if (!val)
        error("_Static_assert failure: %s", tok->sval);
}

/*----------------------------------------------------------------------
 * Expression
 */

static Node *read_var_or_func(char *name) {
    Node *v = dict_get(localenv ? localenv : globalenv, name);
    if (!v || v->ctype->type == CTYPE_FUNC)
        return ast_funcdesg(name, v);
    return v;
}

static Node *convert_funcdesg(Node *node) {
    if (!node)
        return NULL;
    if (node->type == AST_FUNCDESG)
        return ast_uop(AST_ADDR, make_ptr_type(node->fptr->ftype), node->fptr);
    return node;
}

static int get_compound_assign_op(Token *tok) {
    if (tok->type != TPUNCT)
        return 0;
    switch (tok->punct) {
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

static Node *read_primary_expr(void) {
    Token *tok = read_token();
    if (!tok) return NULL;
    if (is_punct(tok, '(')) {
        Node *r = read_expr();
        expect(')');
        return r;
    }
    if (is_punct(tok, KGENERIC)) {
        return read_generic();
    }
    switch (tok->type) {
    case TIDENT:
        return read_var_or_func(tok->sval);
    case TNUMBER:
        return read_number(tok->sval);
    case TCHAR:
        return ast_inttype(ctype_int, tok->c);
    case TSTRING:
        return ast_string(tok->sval);
    case TPUNCT:
        unget_token(tok);
        return NULL;
    default:
        error("internal error: unknown token type: %d", tok->type);
    }
}

static Node *read_subscript_expr(Node *node) {
    Node *sub = read_expr();
    if (!sub)
        error("subscription expected");
    expect(']');
    Node *t = usual_conv('+', node, sub);
    return ast_uop(AST_DEREF, t->ctype->ptr, t);
}

static Node *read_postfix_expr_tail(Node *node) {
    if (!node) return NULL;
    for (;;) {
        if (next_token('(')) {
            Ctype *t = node->ctype;
            if (t->type == CTYPE_PTR && t->ptr->type == CTYPE_FUNC)
                return read_funcptr_call(node);
            if (node->type != AST_FUNCDESG)
                error("function name expected, but got %s", a2s(node));
            node = read_funcall(node->fname, node->fptr);
            continue;
        }
        if (node->type == AST_FUNCDESG && !node->fptr)
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
            if (node->ctype->type != CTYPE_PTR)
                error("pointer type expected, but got %s %s",
                      c2s(node->ctype), a2s(node));
            node = ast_uop(AST_DEREF, node->ctype->ptr, node);
            node = read_struct_field(node);
            continue;
        }
        Token *tok = peek_token();
        if (next_token(OP_INC) || next_token(OP_DEC)) {
            ensure_lvalue(node);
            int op = is_punct(tok, OP_INC) ? OP_POST_INC : OP_POST_DEC;
            return ast_uop(op, node->ctype, node);
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
    operand = convert_funcdesg(operand);
    ensure_lvalue(operand);
    return ast_uop(op, operand->ctype, operand);
}

static Node *read_label_addr(void) {
    Token *tok = read_token();
    if (tok->type != TIDENT)
        error("Label name expected after &&, but got %s", t2s(tok));
    Node *r = ast_label_addr(tok->sval);
    list_push(gotos, r);
    return r;
}

static Node *read_unary_addr(void) {
    Node *operand = read_cast_expr();
    if (operand->type == AST_FUNCDESG)
        return operand;
    ensure_lvalue(operand);
    return ast_uop(AST_ADDR, make_ptr_type(operand->ctype), operand);
}

static Node *read_unary_deref(void) {
    Node *operand = read_cast_expr();
    operand = convert_funcdesg(operand);
    Ctype *ctype = convert_array(operand->ctype);
    if (ctype->type != CTYPE_PTR)
        error("pointer type expected, but got %s", a2s(operand));
    if (ctype->ptr->type == CTYPE_FUNC)
        return operand;
    return ast_uop(AST_DEREF, operand->ctype->ptr, operand);
}

static Node *read_unary_minus(void) {
    Node *expr = read_cast_expr();
    ensure_arithtype(expr);
    return ast_uop(OP_UMINUS, expr->ctype, expr);
}

static Node *read_unary_bitnot(void) {
    Node *expr = read_cast_expr();
    expr = convert_funcdesg(expr);
    if (!is_inttype(expr->ctype))
        error("invalid use of ~: %s", a2s(expr));
    return ast_uop('~', expr->ctype, expr);
}

static Node *read_unary_lognot(void) {
    Node *operand = read_cast_expr();
    operand = convert_funcdesg(operand);
    return ast_uop('!', ctype_int, operand);
}

static Node *read_unary_expr(void) {
    Token *tok = read_token();
    if (tok->type == TPUNCT) {
        switch (tok->punct) {
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

static Node *read_compound_literal(Ctype *ctype) {
    char *name = make_label();
    List *init = read_decl_init(ctype);
    Node *r = ast_lvar(ctype, name);
    r->lvarinit = init;
    return r;
}

static Ctype *read_cast_type(void) {
    Ctype *basetype = read_decl_spec(NULL);
    return read_declarator(NULL, basetype, NULL, DECL_CAST);
}

static Node *read_cast_expr(void) {
    Token *tok = read_token();
    if (is_punct(tok, '(') && is_type_keyword(peek_token())) {
        Ctype *ctype = read_cast_type();
        expect(')');
        if (is_punct(peek_token(), '{')) {
            Node *node = read_compound_literal(ctype);
            return read_postfix_expr_tail(node);
        }
        return ast_uop(OP_CAST, ctype, read_cast_expr());
    }
    unget_token(tok);
    return read_unary_expr();
}

static Node *read_multiplicative_expr(void) {
    Node *node = read_cast_expr();
    node = convert_funcdesg(node);
    for (;;) {
        if      (next_token('*')) node = usual_conv('*', node, convert_funcdesg(read_cast_expr()));
        else if (next_token('/')) node = usual_conv('/', node, convert_funcdesg(read_cast_expr()));
        else if (next_token('%')) node = usual_conv('%', node, convert_funcdesg(read_cast_expr()));
        else break;
    }
    return node;
}

static Node *read_additive_expr(void) {
    Node *node = read_multiplicative_expr();
    for (;;) {
        if      (next_token('+')) node = usual_conv('+', node, read_multiplicative_expr());
        else if (next_token('-')) node = usual_conv('-', node, read_multiplicative_expr());
        else break;
    }
    return node;
}

static Node *read_shift_expr(void) {
    Node *node = read_additive_expr();
    for (;;) {
        int op;
        if (next_token(OP_SAL))
            op = OP_SAL;
        else if (next_token(OP_SAR))
            op = node->ctype->sig ? OP_SAR : OP_SHR;
        else
            break;
        Node *right = read_additive_expr();
        ensure_inttype(node);
        ensure_inttype(right);
        Ctype *resulttype = larger_type(node->ctype, right->ctype);
        node = ast_binop(resulttype, op, node, right);
    }
    return node;
}

static Node *read_relational_expr(void) {
    Node *node = read_shift_expr();
    for (;;) {
        if      (next_token('<'))   node = usual_conv('<',   node, read_shift_expr());
        else if (next_token('>'))   node = usual_conv('>',   node, read_shift_expr());
        else if (next_token(OP_LE)) node = usual_conv(OP_LE, node, read_shift_expr());
        else if (next_token(OP_GE)) node = usual_conv(OP_GE, node, read_shift_expr());
        else break;
    }
    return node;
}

static Node *read_equality_expr(void) {
    Node *node = read_relational_expr();
    if (next_token(OP_EQ))
        return usual_conv(OP_EQ, node, read_equality_expr());
    if (next_token(OP_NE))
        return usual_conv(OP_NE, node, read_equality_expr());
    return node;
}

static Node *read_bitand_expr(void) {
    Node *node = read_equality_expr();
    while (next_token('&'))
        node = usual_conv('&', node, read_equality_expr());
    return node;
}

static Node *read_bitxor_expr(void) {
    Node *node = read_bitand_expr();
    while (next_token('^'))
        node = usual_conv('^', node, read_bitand_expr());
    return node;
}

static Node *read_bitor_expr(void) {
    Node *node = read_bitxor_expr();
    while (next_token('|'))
        node = usual_conv('|', node, read_bitxor_expr());
    return node;
}

static Node *read_logand_expr(void) {
    Node *node = read_bitor_expr();
    while (next_token(OP_LOGAND))
        node = ast_binop(ctype_int, OP_LOGAND, node, read_bitor_expr());
    return node;
}

static Node *read_logor_expr(void) {
    Node *node = read_logand_expr();
    while (next_token(OP_LOGOR))
        node = ast_binop(ctype_int, OP_LOGOR, node, read_logand_expr());
    return node;
}

static Node *read_conditional_expr(void) {
    Node *node = read_logor_expr();
    if (!next_token('?'))
        return node;
    Node *then = read_comma_expr();
    expect(':');
    Node *els = read_conditional_expr();
    return ast_ternary(els->ctype, node, then, els);
}

static Node *read_assignment_expr(void) {
    Node *node = read_logor_expr();
    Token *tok = read_token();
    if (!tok)
        return node;
    if (is_punct(tok, '?')) {
        Node *then = read_comma_expr();
        expect(':');
        Node *els = read_conditional_expr();
        return ast_ternary(els->ctype, node, then, els);
    }
    int cop = get_compound_assign_op(tok);
    if (is_punct(tok, '=') || cop) {
        Node *value = read_assignment_expr();
        if (is_punct(tok, '=') || cop)
            ensure_lvalue(node);
        Node *right = cop ? usual_conv(cop, node, value) : value;
        if (is_arithtype(node->ctype) && node->ctype->type != right->ctype->type)
            right = ast_conv(node->ctype, right);
        return ast_binop(node->ctype, '=', node, right);
    }
    unget_token(tok);
    return node;
}

static Node *read_comma_expr(void) {
    Node *node = read_assignment_expr();
    while (next_token(',')) {
        Node *expr = read_assignment_expr();
        node = ast_binop(expr->ctype, ',', node, expr);
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

/*----------------------------------------------------------------------
 * Struct or union
 */

static Node *read_struct_field(Node *struc) {
    if (struc->ctype->type != CTYPE_STRUCT)
        error("struct expected, but got %s", a2s(struc));
    Token *name = read_token();
    if (name->type != TIDENT)
        error("field name expected, but got %s", t2s(name));
    Ctype *field = dict_get(struc->ctype->fields, name->sval);
    if (!field)
        error("struct has no such field: %s", t2s(name));
    return ast_struct_ref(field, struc, name->sval);
}

static char *read_rectype_tag(void) {
    Token *tok = read_token();
    if (tok->type == TIDENT)
        return tok->sval;
    unget_token(tok);
    return NULL;
}

static int compute_padding(int offset, Ctype *ctype) {
    int size = get_alignment(ctype);
    return (offset % size == 0) ? 0 : size - offset % size;
}

static void squash_unnamed_struct(Dict *dict, Ctype *unnamed, int offset) {
    for (Iter *i = list_iter(dict_keys(unnamed->fields)); !iter_end(i);) {
        char *name = iter_next(i);
        Ctype *type = copy_type(dict_get(unnamed->fields, name));
        type->offset += offset;
        dict_put(dict, name, type);
    }
}

static int maybe_read_bitsize(char *name, Ctype *ctype) {
    if (!next_token(':'))
        return -1;
    if (!is_inttype(ctype))
        error("non-integer type cannot be a bitfield: %s", c2s(ctype));
    int r = read_intexpr();
    int maxsize = ctype->type == CTYPE_BOOL ? 1 : ctype->size * 8;
    if (r < 0 || maxsize < r)
        error("invalid bitfield size for %s: %d", c2s(ctype), r);
    if (r == 0 && name != NULL)
        error("zero-width bitfield needs to be unnamed: %s", name);
    return r;
}

static List *read_rectype_fields_sub(void) {
    List *r = make_list();
    for (;;) {
        if (next_token(KSTATIC_ASSERT)) {
            read_static_assert();
            continue;
        }
        if (!is_type_keyword(peek_token()))
            break;
        Ctype *basetype = read_decl_spec(NULL);
        if (basetype->type == CTYPE_STRUCT && next_token(';')) {
            list_push(r, make_pair(NULL, basetype));
            continue;
        }
        for (;;) {
            char *name = NULL;
            Ctype *fieldtype = read_declarator(&name, basetype, NULL, DECL_PARAM_TYPEONLY);
            ensure_not_void(fieldtype);
            fieldtype = copy_type(fieldtype);
            fieldtype->bitsize = maybe_read_bitsize(name, fieldtype);
            list_push(r, make_pair(name, fieldtype));
            if (next_token(','))
                continue;
            if (is_punct(peek_token(), '}'))
                warn("missing ';' at the end of field list");
            else
                expect(';');
            break;
        }
    }
    expect('}');
    return r;
}

static void fix_rectype_flexible_member(List *fields) {
    Iter *iter = list_iter(fields);
    while (!iter_end(iter)) {
        Pair *pair = iter_next(iter);
        char *name = pair->first;
        Ctype *ctype = pair->second;
        if (ctype->type != CTYPE_ARRAY)
            continue;
        if (ctype->len == -1) {
            if (!iter_end(iter))
                error("flexible member may only appear as the last member: %s %s", c2s(ctype), name);
            if (list_len(fields) == 1)
                error("flexible member with no other fields: %s %s", c2s(ctype), name);
            ctype->size = 0;
        }
    }
}

static void finish_bitfield(int *off, int *bitoff) {
    *off += (*bitoff + 8) / 8;
    *bitoff = -1;
}

static Dict *update_struct_offset(List *fields, int *rsize) {
    int off = 0, bitoff = -1;
    Iter *iter = list_iter(fields);
    Dict *r = make_dict(NULL);
    while (!iter_end(iter)) {
        Pair *pair = iter_next(iter);
        char *name = pair->first;
        Ctype *fieldtype = pair->second;
        if (name == NULL && fieldtype->type == CTYPE_STRUCT) {
            finish_bitfield(&off, &bitoff);
            squash_unnamed_struct(r, fieldtype, off);
            off += fieldtype->size;
            continue;
        }
        if (fieldtype->bitsize == 0) {
            finish_bitfield(&off, &bitoff);
            off += compute_padding(off, fieldtype);
            bitoff = 0;
            continue;
        }
        if (fieldtype->bitsize >= 0) {
            int room = fieldtype->size * 8 - bitoff;
            if (0 <= bitoff && fieldtype->bitsize <= room) {
                fieldtype->bitoff = bitoff;
                fieldtype->offset = off;
            } else {
                finish_bitfield(&off, &bitoff);
                off += compute_padding(off, fieldtype);
                fieldtype->offset = off;
                fieldtype->bitoff = 0;
            }
            bitoff = fieldtype->bitsize;
        } else {
            finish_bitfield(&off, &bitoff);
            off += compute_padding(off, fieldtype);
            fieldtype->offset = off;
            off += fieldtype->size;
        }
        dict_put(r, name, fieldtype);
    }
    finish_bitfield(&off, &bitoff);
    *rsize = off;
    return r;
}

static Dict *update_union_offset(List *fields, int *rsize) {
    int maxsize = 0;
    Iter *iter = list_iter(fields);
    Dict *r = make_dict(NULL);
    while (!iter_end(iter)) {
        Pair *pair = iter_next(iter);
        char *name = pair->first;
        Ctype *fieldtype = pair->second;
        maxsize = MAX(maxsize, fieldtype->size);
        if (name == NULL && fieldtype->type == CTYPE_STRUCT) {
            squash_unnamed_struct(r, fieldtype, 0);
            continue;
        }
        fieldtype->offset = 0;
        if (fieldtype->bitsize >= 0)
            fieldtype->bitoff = 0;
        dict_put(r, name, fieldtype);
    }
    *rsize = maxsize;
    return r;
}

static Dict *read_rectype_fields(int *rsize, bool is_struct) {
    if (!next_token('{'))
        return NULL;
    List *fields = read_rectype_fields_sub();
    fix_rectype_flexible_member(fields);
    return is_struct
        ? update_struct_offset(fields, rsize)
        : update_union_offset(fields, rsize);
}

static Ctype *read_rectype_def(Dict *env, bool is_struct) {
    char *tag = read_rectype_tag();
    Ctype *r;
    if (tag) {
        r = dict_get(env, tag);
        if (!r) {
            r = make_struct_type(NULL, 0, is_struct);
            dict_put(env, tag, r);
        }
    } else {
        r = make_struct_type(NULL, 0, is_struct);
    }
    int size = 0;
    Dict *fields = read_rectype_fields(&size, is_struct);
    if (r && !fields)
        return r;
    if (r && fields) {
        r->fields = fields;
        r->size = size;
        return r;
    }
    return r;
}

static Ctype *read_struct_def(void) {
    return read_rectype_def(struct_defs, true);
}

static Ctype *read_union_def(void) {
    return read_rectype_def(union_defs, false);
}

/*----------------------------------------------------------------------
 * Enum
 */

static Ctype *read_enum_def(void) {
    Token *tok = read_token();
    if (tok->type == TIDENT)
        tok = read_token();
    if (!is_punct(tok, '{')) {
        unget_token(tok);
        return ctype_int;
    }
    int val = 0;
    for (;;) {
        tok = read_token();
        if (is_punct(tok, '}'))
            break;
        if (tok->type != TIDENT)
            error("Identifier expected, but got %s", t2s(tok));
        char *name = tok->sval;

        if (next_token('='))
            val = read_intexpr();
        Node *constval = ast_inttype(ctype_int, val++);
        dict_put(localenv ? localenv : globalenv, name, constval);
        if (next_token(','))
            continue;
        if (next_token('}'))
            break;
        error("',' or '}' expected, but got %s", t2s(read_token()));
    }
    return ctype_int;
}

/*----------------------------------------------------------------------
 * Initializer
 */

static void assign_string(List *inits, Ctype *ctype, char *p, int off) {
    if (ctype->len == -1)
        ctype->len = ctype->size = strlen(p) + 1;
    int i = 0;
    for (; i < ctype->len && *p; i++)
        list_push(inits, ast_init(ast_inttype(ctype_char, *p++), ctype_char, off + i));
    for (; i < ctype->len; i++)
        list_push(inits, ast_init(ast_inttype(ctype_char, 0), ctype_char, off + i));
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
        warn("ignore excessive initializer: %s", a2s(ignore));
        maybe_skip_comma();
    }
}

static void read_initializer_elem(List *inits, Ctype *ctype, int off) {
    if (ctype->type == CTYPE_ARRAY || ctype->type == CTYPE_STRUCT) {
        read_initializer_list(inits, ctype, off);
    } else {
        Node *expr = read_assignment_expr();
        ensure_assignable(ctype, expr->ctype);
        list_push(inits, ast_init(expr, ctype, off));
    }
}

static void read_struct_initializer(List *inits, Ctype *ctype, int off) {
    bool has_brace = maybe_read_brace();
    Iter *iter = list_iter(dict_keys(ctype->fields));
    Dict *written = make_dict(NULL);
    for (;;) {
        Token *tok = read_token();
        if (is_punct(tok, '}')) {
            if (!has_brace)
                unget_token(tok);
            return;
        }
        char *fieldname;
        Ctype *fieldtype;
        if (is_punct(tok, '.')) {
            tok = read_token();
            if (!tok || tok->type != TIDENT)
                error("malformed desginated initializer: %s", t2s(tok));
            fieldname = tok->sval;
            fieldtype = dict_get(ctype->fields, fieldname);
            if (!fieldtype)
                error("field does not exist: %s", t2s(tok));
            expect('=');
            iter = list_iter(dict_keys(ctype->fields));
            while (!iter_end(iter)) {
                char *s = iter_next(iter);
                if (strcmp(fieldname, s) == 0)
                    break;
            }
        } else {
            unget_token(tok);
            if (iter_end(iter))
                break;
            fieldname = iter_next(iter);
            fieldtype = dict_get(ctype->fields, fieldname);
        }
        if (dict_get(written, fieldname))
            error("struct field initialized twice: %s", fieldname);
        dict_put(written, fieldname, (void *)1);
        read_initializer_elem(inits, fieldtype, off + fieldtype->offset);
        maybe_skip_comma();
        if (!ctype->is_struct)
            break;
    }
    if (has_brace)
        skip_to_brace();
}

static void read_array_initializer(List *inits, Ctype *ctype, int off) {
    bool has_brace = maybe_read_brace();
    bool incomplete = (ctype->len == -1);
    int elemsize = ctype->ptr->size;
    int i;
    for (i = 0; incomplete || i < ctype->len; i++) {
        Token *tok = read_token();
        if (is_punct(tok, '}')) {
            if (!has_brace)
                unget_token(tok);
            goto finish;
        }
        unget_token(tok);
        read_initializer_elem(inits, ctype->ptr, off + elemsize * i);
        maybe_skip_comma();
    }
    if (has_brace)
        skip_to_brace();
 finish:
    if (incomplete){
        ctype->len = i;
        ctype->size = elemsize * i;
    }
}

static void read_initializer_list(List *inits, Ctype *ctype, int off) {
    Token *tok = read_token();
    if (is_string(ctype)) {
        if (tok->type == TSTRING) {
            assign_string(inits, ctype, tok->sval, off);
            return;
        }
        if (is_punct(tok, '{') && peek_token()->type == TSTRING) {
            tok = read_token();
            assign_string(inits, ctype, tok->sval, off);
            expect('}');
            return;
        }
    }
    unget_token(tok);
    if (ctype->type == CTYPE_ARRAY) {
        read_array_initializer(inits, ctype, off);
    } else if (ctype->type == CTYPE_STRUCT) {
        read_struct_initializer(inits, ctype, off);
    } else {
        Ctype *arraytype = make_array_type(ctype, 1);
        read_array_initializer(inits, arraytype, off);
    }
}

static List *read_decl_init(Ctype *ctype) {
    List *r = make_list();
    if (is_punct(peek_token(), '{') || is_string(ctype)) {
        read_initializer_list(r, ctype, 0);
    } else {
        Node *init = read_assignment_expr();
        if (is_arithtype(init->ctype) && init->ctype->type != ctype->type)
            init = ast_conv(ctype, init);
        list_push(r, ast_init(init, ctype, 0));
    }
    return r;
}

/*----------------------------------------------------------------------
 * Declarator
 *
 * C's syntax for declaration is not only hard to read for humans but also
 * hard to parse for hand-written parsers. Consider the following two cases:
 *
 *   (A) int *x;
 *   (B) int *x();
 *
 * A is of type pointer to int, while B is not pointer type. B is of type
 * function returning a pointer to an integer. The meaning of the first half
 * of the declaration ("int *" part) is different among them.
 *
 * In 8cc, delcarations are parsed by two functions: read_direct_declarator1
 * and read_direct_declarator2. The former function parses the first half of a
 * declaration, and the latter parses the (possibly nonexistent) parentheses
 * of a function or an array.
 */

static Ctype *read_func_param(char **name, bool optional) {
    int sclass;
    Ctype *basetype = read_decl_spec(&sclass);
    return read_declarator(name, basetype, NULL, optional ? DECL_PARAM_TYPEONLY : DECL_PARAM);
}

static Ctype *read_func_param_list(List *paramvars, Ctype *rettype) {
    bool typeonly = !paramvars;
    List *paramtypes = make_list();
    Token *tok = read_token();
    if (is_punct(tok, KVOID) && next_token(')'))
        return make_func_type(rettype, paramtypes, false);
    if (is_punct(tok, ')'))
        return make_func_type(rettype, paramtypes, true);
    unget_token(tok);
    for (;;) {
        if (next_token(KTHREEDOTS)) {
            if (list_len(paramtypes) == 0)
                error("at least one parameter is required");
            expect(')');
            return make_func_type(rettype, paramtypes, true);
        }
        char *name;
        Ctype *ptype = read_func_param(&name, typeonly);
        ensure_not_void(ptype);
        if (ptype->type == CTYPE_ARRAY)
            ptype = make_ptr_type(ptype->ptr);
        list_push(paramtypes, ptype);
        if (!typeonly) {
            Node *node = ast_lvar(ptype, name);
            list_push(paramvars, node);
        }
        Token *tok = read_token();
        if (is_punct(tok, ')'))
            return make_func_type(rettype, paramtypes, false);
        if (!is_punct(tok, ','))
            error("comma expected, but got %s", t2s(tok));
    }
}

static Ctype *read_direct_declarator2(Ctype *basetype, List *params) {
    if (next_token('[')) {
        int len;
        if (next_token(']')) {
            len = -1;
        } else {
            len = read_intexpr();
            expect(']');
        }
        Ctype *t = read_direct_declarator2(basetype, params);
        if (t->type == CTYPE_FUNC)
            error("array of functions");
        return make_array_type(t, len);
    }
    if (next_token('(')) {
        if (basetype->type == CTYPE_FUNC)
            error("function returning an function");
        if (basetype->type == CTYPE_ARRAY)
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

static Ctype *read_direct_declarator1(char **rname, Ctype *basetype, List *params, int ctx) {
    Token *tok = read_token();
    Token *next = peek_token();
    if (is_punct(tok, '(') && !is_type_keyword(next) && !is_punct(next, ')')) {
        Ctype *stub = make_stub_type();
        Ctype *t = read_direct_declarator1(rname, stub, params, ctx);
        expect(')');
        *stub = *read_direct_declarator2(basetype, params);
        return t;
    }
    if (is_punct(tok, '*')) {
        skip_type_qualifiers();
        return read_direct_declarator1(rname, make_ptr_type(basetype), params, ctx);
    }
    if (tok->type == TIDENT) {
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

static void fix_array_size(Ctype *t) {
    assert(t->type != CTYPE_STUB);
    if (t->type == CTYPE_ARRAY) {
        fix_array_size(t->ptr);
        t->size = t->len * t->ptr->size;
    } else if (t->type == CTYPE_PTR) {
        fix_array_size(t->ptr);
    } else if (t->type == CTYPE_FUNC) {
        fix_array_size(t->rettype);
    }
}

static Ctype *read_declarator(char **rname, Ctype *basetype, List *params, int ctx) {
    Ctype *t = read_direct_declarator1(rname, basetype, params, ctx);
    fix_array_size(t);
    return t;
}

/*----------------------------------------------------------------------
 * typeof()
 */

static Ctype *read_typeof(void) {
    expect('(');
    Ctype *r = is_type_keyword(peek_token())
        ? read_cast_type()
        : read_comma_expr()->ctype;
    expect(')');
    return r;
}

/*----------------------------------------------------------------------
 * Declaration specifier
 */

static Ctype *read_decl_spec(int *rsclass) {
    int sclass = 0;
    Token *tok = peek_token();
    if (!is_type_keyword(tok))
        error("type keyword expected, but got %s", t2s(tok));

#define unused __attribute__((unused))
    bool kconst unused = false, kvolatile unused = false, kinline unused = false;
#undef unused
    Ctype *usertype = NULL;
    enum { kvoid = 1, kbool, kchar, kint, kfloat, kdouble } type = 0;
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
            if (type == kbool && (size != 0 && sig != 0))               \
                goto err;                                               \
            if (size == kshort && (type != 0 && type != kint))          \
                goto err;                                               \
            if (size == klong && (type != 0 && type != kint && type != kdouble)) \
                goto err;                                               \
            if (sig != 0 && (type == kvoid || type == kfloat || type == kdouble)) \
                goto err;                                               \
            if (usertype && (type != 0 || size != 0 || sig != 0))       \
                goto err;                                               \
        } while (0)

        tok = read_token();
        if (!tok)
            error("premature end of input");
        if (tok->type == TIDENT && !usertype) {
            Ctype *def = get_typedef(tok->sval);
            if (def) {
                set(usertype, def);
                continue;
            }
        }
        if (tok->type != TPUNCT) {
            unget_token(tok);
            break;
        }
        switch (tok->punct) {
        case KTYPEDEF:    setsclass(S_TYPEDEF); continue;
        case KEXTERN:     setsclass(S_EXTERN); continue;
        case KSTATIC:     setsclass(S_STATIC); continue;
        case KAUTO:       setsclass(S_AUTO); continue;
        case KREGISTER:   setsclass(S_REGISTER); continue;
        case KCONST:      kconst = 1; continue;
        case KVOLATILE:   kvolatile = 1; continue;
        case KINLINE:     kinline = 1; continue;
        case KVOID:       set(type, kvoid); continue;
        case KBOOL:       set(type, kbool); continue;
        case KCHAR:       set(type, kchar); continue;
        case KINT:        set(type, kint); continue;
        case KFLOAT:      set(type, kfloat); continue;
        case KDOUBLE:     set(type, kdouble); continue;
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
    switch (type) {
    case kvoid:   return ctype_void;
    case kbool:   return make_numtype(CTYPE_BOOL, false);
    case kchar:   return make_numtype(CTYPE_CHAR, sig != kunsigned);
    case kfloat:  return make_numtype(CTYPE_FLOAT, false);
    case kdouble: return make_numtype(size == klong ? CTYPE_LDOUBLE : CTYPE_DOUBLE, false);
    default: break;
    }
    switch (size) {
    case kshort: return make_numtype(CTYPE_SHORT, sig != kunsigned);
    case klong:  return make_numtype(CTYPE_LONG, sig != kunsigned);
    case kllong: return make_numtype(CTYPE_LLONG, sig != kunsigned);
    default:     return make_numtype(CTYPE_INT, sig != kunsigned);
    }
    error("internal error: type: %d, size: %d", type, size);
 err:
    error("type mismatch: %s", t2s(tok));
}

/*----------------------------------------------------------------------
 * Declaration
 */

static void read_decl(List *block, MakeVarFn *make_var) {
    int sclass;
    Ctype *basetype = read_decl_spec(&sclass);
    if (next_token(';'))
        return;
    for (;;) {
        char *name = NULL;
        Ctype *ctype = read_declarator(&name, copy_incomplete_type(basetype), NULL, DECL_BODY);
        ctype->isstatic = (sclass == S_STATIC);
        Token *tok = read_token();
        if (is_punct(tok, '=')) {
            if (sclass == S_TYPEDEF)
                error("= after typedef");
            ensure_not_void(ctype);
            Node *var = make_var(ctype, name);
            list_push(block, ast_decl(var, read_decl_init(var->ctype)));
            tok = read_token();
        } else if (sclass == S_TYPEDEF) {
            ast_typedef(ctype, name);
        } else if (ctype->type == CTYPE_FUNC) {
            make_var(ctype, name);
        } else {
            Node *var = make_var(ctype, name);
            if (sclass != S_EXTERN)
                list_push(block, ast_decl(var, NULL));
        }
        if (is_punct(tok, ';'))
            return;
        if (!is_punct(tok, ','))
            error("';' or ',' are expected, but got %s", t2s(tok));
    }
}

/*----------------------------------------------------------------------
 * Function definition
 */

static Node *read_func_body(Ctype *functype, char *fname, List *params) {
    localenv = make_dict(localenv);
    localvars = make_list();
    current_func_type = functype;
    Node *funcname = ast_string(fname);
    dict_put(localenv, "__func__", funcname);
    dict_put(localenv, "__FUNCTION__", funcname);
    Node *body = read_compound_stmt();
    Node *r = ast_func(functype, fname, params, body, localvars);
    current_func_type = NULL;
    localenv = NULL;
    localvars = NULL;
    return r;
}

static bool is_funcdef(void) {
    List *buf = make_list();
    int nest = 0;
    bool paren = false;
    bool r = true;
    for (;;) {
        Token *tok = read_token();
        list_push(buf, tok);
        if (!tok)
            error("premature end of input");
        if (nest == 0 && paren && is_punct(tok, '{'))
            break;
        if (nest == 0 && (is_punct(tok, ';') || is_punct(tok, ',') || is_punct(tok, '='))) {
            r = false;
            break;
        }
        if (is_punct(tok, '(')) {
            nest++;
        }
        if (is_punct(tok, ')')) {
            if (nest == 0)
                error("extra close parenthesis");
            paren = true;
            nest--;
        }
    }
    while (list_len(buf) > 0)
        unget_token(list_pop(buf));
    return r;
}

static void backfill_labels(void) {
    for (Iter *i = list_iter(gotos); !iter_end(i);) {
        Node *src = iter_next(i);
        char *label = src->label;
        Node *dst = dict_get(labels, label);
        if (!dst)
            error("stray %s: %s", src->type == AST_GOTO ? "goto" : "unary &&", label);
        if (dst->newlabel)
            src->newlabel = dst->newlabel;
        else
            src->newlabel = dst->newlabel = make_label();
    }
}

static Node *read_funcdef(void) {
    int sclass;
    Ctype *basetype = read_decl_spec(&sclass);
    localenv = make_dict(globalenv);
    gotos = make_list();
    labels = make_dict(NULL);
    char *name;
    List *params = make_list();
    Ctype *functype = read_declarator(&name, basetype, params, DECL_BODY);
    functype->isstatic = (sclass == S_STATIC);
    ast_gvar(functype, name);
    expect('{');
    Node *r = read_func_body(functype, name, params);
    backfill_labels();
    localenv = NULL;
    return r;
}

/*----------------------------------------------------------------------
 * If
 */

static Node *read_boolean_expr(void) {
    Node *cond = read_expr();
    return is_flotype(cond->ctype) ? ast_conv(ctype_bool, cond) : cond;
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

/*----------------------------------------------------------------------
 * For
 */

static Node *read_opt_decl_or_stmt(void) {
    if (next_token(';'))
        return NULL;
    List *list = make_list();
    read_decl_or_stmt(list);
    return list_shift(list);
}

static Node *read_for_stmt(void) {
    expect('(');
    localenv = make_dict(localenv);
    Node *init = read_opt_decl_or_stmt();
    Node *cond = read_expr_opt();
    if (cond && is_flotype(cond->ctype))
        cond = ast_conv(ctype_bool, cond);
    expect(';');
    Node *step = read_expr_opt();
    expect(')');
    Node *body = read_stmt();
    localenv = dict_parent(localenv);
    return ast_for(init, cond, step, body);
}

/*----------------------------------------------------------------------
 * While
 */

static Node *read_while_stmt(void) {
    expect('(');
    Node *cond = read_boolean_expr();
    expect(')');
    Node *body = read_stmt();
    return ast_while(cond, body);
}

/*----------------------------------------------------------------------
 * Do
 */

static Node *read_do_stmt(void) {
    Node *body = read_stmt();
    Token *tok = read_token();
    if (!is_punct(tok, KWHILE))
        error("'while' is expected, but got %s", t2s(tok));
    expect('(');
    Node *cond = read_boolean_expr();
    expect(')');
    expect(';');
    return ast_do(cond, body);
}

/*----------------------------------------------------------------------
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
    int end;
    if (next_token(KTHREEDOTS))
        end = read_intexpr();
    else
        end = beg;
    expect(':');
    if (beg > end)
        error("case region is not in correct order: %d %d", beg, end);
    return ast_case(beg, end);
}

static Node *read_default_label(void) {
    expect(':');
    return make_ast(&(Node){ AST_DEFAULT });
}

/*----------------------------------------------------------------------
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
        Node *expr = read_cast_expr();
        if (expr->ctype->type != CTYPE_PTR)
            error("pointer expected for computed goto, but got %s", a2s(expr));
        return ast_computed_goto(expr);
    }
    Token *tok = read_token();
    if (!tok || tok->type != TIDENT)
        error("identifier expected, but got %s", t2s(tok));
    expect(';');
    Node *r = ast_goto(tok->sval);
    list_push(gotos, r);
    return r;
}

static Node *read_label(Token *tok) {
    expect(':');
    char *label = tok->sval;
    Node *r = ast_label(label);
    if (dict_get(labels, label))
        error("duplicate label: %s", t2s(tok));
    dict_put(labels, label, r);
    return r;
}

/*----------------------------------------------------------------------
 * Statement
 */

static Node *read_stmt(void) {
    Token *tok = read_token();
    if (tok->type == TPUNCT) {
        switch (tok->punct) {
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
    if (tok->type == TIDENT && is_punct(peek_token(), ':'))
        return read_label(tok);
    unget_token(tok);
    Node *r = read_expr_opt();
    expect(';');
    return r;
}

static Node *read_compound_stmt(void) {
    localenv = make_dict(localenv);
    List *list = make_list();
    for (;;) {
        if (next_token('}'))
            break;
        read_decl_or_stmt(list);
    }
    localenv = dict_parent(localenv);
    return ast_compound_stmt(list);
}

static void read_decl_or_stmt(List *list) {
    Token *tok = peek_token();
    if (tok == NULL)
        error("premature end of input");
    if (is_type_keyword(tok)) {
        read_decl(list, ast_lvar);
    } else if (next_token(KSTATIC_ASSERT)) {
        read_static_assert();
    } else {
        Node *stmt = read_stmt();
        if (stmt)
            list_push(list, stmt);
    }
}

/*----------------------------------------------------------------------
 * Compilation unit
 */

List *read_toplevels(void) {
    List *r = make_list();
    for (;;) {
        if (!peek_token())
            return r;
        if (is_funcdef())
            list_push(r, read_funcdef());
        else
            read_decl(r, ast_gvar);
    }
}

/*----------------------------------------------------------------------
 * Initializer
 */

void parse_init(void) {
    Ctype *t = make_func_type(ctype_void, make_list(), true);
    dict_put(globalenv, "__builtin_va_start", ast_gvar(t, "__builtin_va_start"));
    dict_put(globalenv, "__builtin_va_arg", ast_gvar(t, "__builtin_va_arg"));
}
