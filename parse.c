#include <ctype.h>
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "8cc.h"

#define MAX_ARGS 6
#define MAX_OP_PRIO 16
#define MAX_ALIGN 16

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define SWAP(a, b)                              \
    { typeof(a) tmp = b; b = a; a = tmp; }

List *strings = &EMPTY_LIST;
List *flonums = &EMPTY_LIST;
static Dict *globalenv = &EMPTY_DICT;
static Dict *localenv = NULL;
static Dict *struct_defs = &EMPTY_DICT;
static Dict *union_defs = &EMPTY_DICT;
static Dict *typedefs = &EMPTY_DICT;
static List *localvars = NULL;
static Ctype *current_func_type = NULL;

Ctype *ctype_void = &(Ctype){ CTYPE_VOID, 0, true };
Ctype *ctype_char = &(Ctype){ CTYPE_CHAR, 1, true };
Ctype *ctype_short = &(Ctype){ CTYPE_SHORT, 2, true };
Ctype *ctype_int = &(Ctype){ CTYPE_INT, 4, true };
Ctype *ctype_long = &(Ctype){ CTYPE_LONG, 8, true };
Ctype *ctype_float = &(Ctype){ CTYPE_FLOAT, 4, true };
Ctype *ctype_double = &(Ctype){ CTYPE_DOUBLE, 8, true };
Ctype *ctype_ldouble = &(Ctype){ CTYPE_LDOUBLE, 8, true };
static Ctype *ctype_ulong = &(Ctype){ CTYPE_LONG, 8, false };
static Ctype *ctype_llong = &(Ctype){ CTYPE_LLONG, 8, true };
static Ctype *ctype_ullong = &(Ctype){ CTYPE_LLONG, 8, false };

static int labelseq = 0;

typedef Node *MakeVarFn(Ctype *ctype, char *name);

static Ctype* make_ptr_type(Ctype *ctype);
static Ctype* make_array_type(Ctype *ctype, int size);
static Node *read_compound_stmt(void);
static void read_decl_or_stmt(List *list);
static Node *read_expr_int(int prec);
static Ctype *convert_array(Ctype *ctype);
static Node *read_stmt(void);
static bool is_type_keyword(Token *tok);
static Node *read_unary_expr(void);
static void read_func_param(Ctype **rtype, char **name, bool optional);
static void read_decl(List *toplevel, MakeVarFn make_var);
static Ctype *read_declarator(char **name, Ctype *basetype, List *params, int ctx);
static Ctype *read_decl_spec(int *sclass);
static Node *read_struct_field(Node *struc);

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

static Node *ast_uop(int type, Ctype *ctype, Node *operand) {
    Node *r = malloc(sizeof(Node));
    r->type = type;
    r->ctype = ctype;
    r->operand = operand;
    return r;
}

static Node *ast_binop(int type, Node *left, Node *right) {
    Node *r = malloc(sizeof(Node));
    r->type = type;
    r->ctype = result_type(type, left->ctype, right->ctype);
    if (type != '=' &&
        convert_array(left->ctype)->type != CTYPE_PTR &&
        convert_array(right->ctype)->type == CTYPE_PTR) {
        r->left = right;
        r->right = left;
    } else {
        r->left = left;
        r->right = right;
    }
    return r;
}

static Node *ast_inttype(Ctype *ctype, long val) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_LITERAL;
    r->ctype = ctype;
    r->ival = val;
    return r;
}

static Node *ast_floattype(Ctype *ctype, double val) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_LITERAL;
    r->ctype = ctype;
    r->fval = val;
    list_push(flonums, r);
    return r;
}

char *make_label(void) {
    return format(".L%d", labelseq++);
}

static Node *ast_lvar(Ctype *ctype, char *name) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_LVAR;
    r->ctype = ctype;
    r->varname = name;
    if (localenv)
        dict_put(localenv, name, r);
    if (localvars)
        list_push(localvars, r);
    return r;
}

static Node *ast_gvar(Ctype *ctype, char *name) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_GVAR;
    r->ctype = ctype;
    r->varname = name;
    r->glabel = name;
    dict_put(globalenv, name, r);
    return r;
}

static Node *ast_string(char *str) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_STRING;
    r->ctype = make_array_type(ctype_char, strlen(str) + 1);
    r->sval = str;
    r->slabel = make_label();
    return r;
}

static Node *ast_funcall(Ctype *ctype, char *fname, List *args, List *paramtypes) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_FUNCALL;
    r->ctype = ctype;
    r->fname = fname;
    r->args = args;
    r->paramtypes = paramtypes;
    return r;
}

static Node *ast_func(Ctype *ctype, char *fname, List *params, Node *body, List *localvars) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_FUNC;
    r->ctype = ctype;
    r->fname = fname;
    r->params = params;
    r->localvars = localvars;
    r->body = body;
    return r;
}

static Node *ast_decl(Node *var, Node *init) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_DECL;
    r->ctype = NULL;
    r->declvar = var;
    r->declinit = init;
    return r;
}

static Node *ast_init_list(List *initlist) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_INIT_LIST;
    r->ctype = NULL;
    r->initlist = initlist;
    return r;
}

static Node *ast_if(Node *cond, Node *then, Node *els) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_IF;
    r->ctype = NULL;
    r->cond = cond;
    r->then = then;
    r->els = els;
    return r;
}

static Node *ast_ternary(Ctype *ctype, Node *cond, Node *then, Node *els) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_TERNARY;
    r->ctype = ctype;
    r->cond = cond;
    r->then = then;
    r->els = els;
    return r;
}

static Node *ast_for_int(int type, Node *init, Node *cond, Node *step, Node *body) {
    Node *r = malloc(sizeof(Node));
    r->type = type;
    r->ctype = NULL;
    r->forinit = init;
    r->forcond = cond;
    r->forstep = step;
    r->forbody = body;
    return r;
}

static Node *ast_for(Node *init, Node *cond, Node *step, Node *body) {
    return ast_for_int(AST_FOR, init, cond, step, body);
}

static Node *ast_while(Node *cond, Node *body) {
    return ast_for_int(AST_WHILE, NULL, cond, NULL, body);
}

static Node *ast_do(Node *cond, Node *body) {
    return ast_for_int(AST_DO, NULL, cond, NULL, body);
}

static Node *ast_return(Ctype *rettype, Node *retval) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_RETURN;
    r->ctype = rettype;
    r->retval = retval;
    return r;
}

static Node *ast_compound_stmt(List *stmts) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_COMPOUND_STMT;
    r->ctype = NULL;
    r->stmts = stmts;
    return r;
}

static Node *ast_struct_ref(Ctype *ctype, Node *struc, char *name) {
    Node *r = malloc(sizeof(Node));
    r->type = AST_STRUCT_REF;
    r->ctype = ctype;
    r->struc = struc;
    r->field = name;
    return r;
}

static Ctype *copy_type(Ctype *ctype) {
    Ctype *r = malloc(sizeof(Ctype));
    memcpy(r, ctype, sizeof(Ctype));
    return r;
}

static Ctype *make_type(int type, bool sig) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = type;
    r->sig = sig;
    if (type == CTYPE_VOID)         r->size = 0;
    else if (type == CTYPE_CHAR)    r->size = 1;
    else if (type == CTYPE_SHORT)   r->size = 2;
    else if (type == CTYPE_INT)     r->size = 4;
    else if (type == CTYPE_LONG)    r->size = 8;
    else if (type == CTYPE_LLONG)   r->size = 8;
    else if (type == CTYPE_FLOAT)   r->size = 8;
    else if (type == CTYPE_DOUBLE)  r->size = 8;
    else if (type == CTYPE_LDOUBLE) r->size = 8;
    else error("internal error");
    return r;
}

static Ctype* make_ptr_type(Ctype *ctype) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = CTYPE_PTR;
    r->ptr = ctype;
    r->size = 8;
    return r;
}

static Ctype* make_array_type(Ctype *ctype, int len) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = CTYPE_ARRAY;
    r->ptr = ctype;
    r->size = (len < 0) ? -1 : ctype->size * len;
    r->len = len;
    return r;
}

static Ctype* make_struct_field_type(Ctype *ctype, int offset) {
    Ctype *r = copy_type(ctype);
    r->offset = offset;
    return r;
}

static Ctype* make_struct_type(Dict *fields, int size) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = CTYPE_STRUCT;
    r->fields = fields;
    r->size = size;
    return r;
}

static Ctype* make_func_type(Ctype *rettype, List *paramtypes, bool has_varargs) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = CTYPE_FUNC;
    r->rettype = rettype;
    r->params = paramtypes;
    r->hasva = has_varargs;
    return r;
}

static Ctype *make_stub_type(void) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = CTYPE_STUB;
    r->size = 0;
    return r;
}

/*----------------------------------------------------------------------
 * Predicates and type checking routines
 */

bool is_ident(Token *tok, char *s) {
    return tok->type == TTYPE_IDENT && !strcmp(tok->sval, s);
}

bool is_inttype(Ctype *ctype) {
    return ctype->type == CTYPE_CHAR || ctype->type == CTYPE_SHORT ||
        ctype->type == CTYPE_INT || ctype->type == CTYPE_LONG || ctype->type == CTYPE_LLONG;
}

bool is_flotype(Ctype *ctype) {
    return ctype->type == CTYPE_FLOAT || ctype->type == CTYPE_DOUBLE ||
        ctype->type == CTYPE_LDOUBLE;
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

static void ensure_not_void(Ctype *ctype) {
    if (ctype->type == CTYPE_VOID)
        error("void is not allowed");
}

static void expect(char punct) {
    Token *tok = read_token();
    if (!is_punct(tok, punct))
        error("'%c' expected, but got %s", punct, t2s(tok));
}

static bool is_right_assoc(Token *tok) {
    return tok->punct == '=';
}

static bool is_type_keyword(Token *tok) {
    if (tok->type != TTYPE_IDENT)
        return false;
    char *keyword[] = {
        "char", "short", "int", "long", "float", "double", "struct",
        "union", "signed", "unsigned", "enum", "void", "typedef", "extern",
        "static", "auto", "register", "const", "volatile", "inline",
        "restrict", "__signed__",
    };
    for (int i = 0; i < sizeof(keyword) / sizeof(*keyword); i++)
        if (!strcmp(keyword[i], tok->sval))
            return true;
    return dict_get(typedefs, tok->sval);
}

/*----------------------------------------------------------------------
 * Type conversion
 */

static Ctype *result_type_int(jmp_buf *jmpbuf, char op, Ctype *a, Ctype *b) {
    if (a->type > b->type)
        SWAP(a, b);
    if (b->type == CTYPE_PTR) {
        if (op == '=')
            return a;
        if (op != '+' && op != '-')
            goto err;
        if (!is_inttype(a))
            goto err;
        return b;
    }
    switch (a->type) {
    case CTYPE_VOID:
        goto err;
    case CTYPE_CHAR:
    case CTYPE_SHORT:
    case CTYPE_INT:
        switch (b->type) {
        case CTYPE_CHAR: case CTYPE_SHORT: case CTYPE_INT:
            return ctype_int;
        case CTYPE_LONG: case CTYPE_LLONG:
            return ctype_long;
        case CTYPE_FLOAT: case CTYPE_DOUBLE: case CTYPE_LDOUBLE:
            return ctype_double;
        case CTYPE_ARRAY: case CTYPE_PTR:
            return b;
        }
        error("internal error");
    case CTYPE_LONG: case CTYPE_LLONG:
        switch (b->type) {
        case CTYPE_LONG: case CTYPE_LLONG:
            return ctype_long;
        case CTYPE_FLOAT: case CTYPE_DOUBLE: case CTYPE_LDOUBLE:
            return ctype_double;
        case CTYPE_ARRAY: case CTYPE_PTR:
            return b;
        }
        error("internal error");
    case CTYPE_FLOAT:
        if (b->type == CTYPE_FLOAT || b->type == CTYPE_DOUBLE || b->type == CTYPE_LDOUBLE)
            return ctype_double;
        goto err;
    case CTYPE_DOUBLE: case CTYPE_LDOUBLE:
        if (b->type == CTYPE_DOUBLE || b->type == CTYPE_LDOUBLE)
            return ctype_double;
        goto err;
    case CTYPE_ARRAY:
        if (b->type != CTYPE_ARRAY)
            goto err;
        return result_type_int(jmpbuf, op, a->ptr, b->ptr);
    default:
        error("internal error: %s %s", c2s(a), c2s(b));
    }
 err:
    longjmp(*jmpbuf, 1);
}

Ctype *result_type(char op, Ctype *a, Ctype *b) {
    jmp_buf jmpbuf;
    if (setjmp(jmpbuf) == 0)
        return result_type_int(&jmpbuf, op, convert_array(a), convert_array(b));
    error("incompatible operands: %c: <%s> and <%s>",
          op, c2s(a), c2s(b));
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
    case OP_CAST: return eval_intexpr(node->operand);
    case AST_TERNARY:
        return eval_intexpr(node->cond) ? eval_intexpr(node->then) : eval_intexpr(node->els);
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
    case OP_LSH: return L << R;
    case OP_RSH: return L >> R;
    case OP_LOGAND: return L && R;
    case OP_LOGOR:  return L || R;
#undef L
#undef R
    default:
        error("Integer expression expected, but got %s", a2s(node));
    }
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
    } else if (s[0] == '0' && s[1] != '\0') {
        base = 8;
        p++;
    }
    while (isxdigit(*p)) {
        if (base == 10 && isalpha(*p))
            error("invalid digit '%c' in a decimal number: %s", *p, s);
        if (base == 8 && !('0' <= *p && *p <= '7'))
            error("invalid digit '%c' in a octal number: %s", *p, s);
        p++;
    }
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
    long val = strtol(s, NULL, base);
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

static Node *get_sizeof_size(bool allow_typename) {
    Token *tok = read_token();
    if (allow_typename && is_type_keyword(tok)) {
        unget_token(tok);
        Ctype *ctype;
        read_func_param(&ctype, NULL, true);
        return ast_inttype(ctype_long, ctype->size);
    }
    if (is_punct(tok, '(')) {
        Node *r = get_sizeof_size(true);
        expect(')');
        return r;
    }
    unget_token(tok);
    Node *expr = read_unary_expr();
    if (expr->ctype->size == 0)
        error("invalid operand for sizeof(): %s type=%s size=%d", a2s(expr), c2s(expr->ctype), expr->ctype->size);
    return ast_inttype(ctype_long, expr->ctype->size);
}

/*----------------------------------------------------------------------
 * Function arguments
 */

static void function_type_check(char *fname, List *params, List *args) {
    if (list_len(args) < list_len(params))
        error("Too few arguments: %s", fname);
    for (Iter *i = list_iter(params), *j = list_iter(args); !iter_end(j);) {
        Ctype *param = iter_next(i);
        Ctype *arg = ((Node *)iter_next(j))->ctype;
        if (param)
            result_type('=', param, arg);
        else
            result_type('=', arg, ctype_int);
    }
}

static Node *read_func_args(char *fname) {
    List *args = make_list();
    for (;;) {
        Token *tok = read_token();
        if (is_punct(tok, ')')) break;
        unget_token(tok);
        list_push(args, read_expr());
        tok = read_token();
        if (is_punct(tok, ')')) break;
        if (!is_punct(tok, ','))
            error("Unexpected token: '%s'", t2s(tok));
    }
    if (MAX_ARGS < list_len(args))
        error("Too many arguments: %s", fname);
    Node *func = dict_get(localenv, fname);
    if (func) {
        Ctype *t = func->ctype;
        if (t->type != CTYPE_FUNC)
            error("%s is not a function, but %s", fname, c2s(t));
        assert(t->params);
        function_type_check(fname, t->params, args);
        return ast_funcall(t->rettype, fname, args, t->params);
    }
    return ast_funcall(ctype_int, fname, args, make_list());
}

/*----------------------------------------------------------------------
 * Expression
 */

static Node *read_var_or_func(char *name) {
    Token *tok = read_token();
    if (is_punct(tok, '('))
        return read_func_args(name);
    unget_token(tok);
    Node *v = dict_get(localenv, name);
    if (!v)
        error("Undefined varaible: %s", name);
    return v;
}

static Node *read_prim(void) {
    Token *tok = read_token();
    if (!tok) return NULL;
    switch (tok->type) {
    case TTYPE_IDENT:
        return read_var_or_func(tok->sval);
    case TTYPE_NUMBER:
        return read_number(tok->sval);
    case TTYPE_CHAR:
        return ast_inttype(ctype_char, tok->c);
    case TTYPE_STRING: {
        Node *r = ast_string(tok->sval);
        list_push(strings, r);
        return r;
    }
    case TTYPE_PUNCT:
        unget_token(tok);
        return NULL;
    default:
        error("internal error: unknown token type: %d", tok->type);
    }
}

static Node *read_subscript_expr(Node *node) {
    Node *sub = read_expr();
    expect(']');
    Node *t = ast_binop('+', node, sub);
    return ast_uop(AST_DEREF, t->ctype->ptr, t);
}

static Node *read_cast(void) {
    Ctype *basetype = read_decl_spec(NULL);
    Ctype *ctype = read_declarator(NULL, basetype, NULL, DECL_CAST);
    expect(')');
    Node *expr = read_expr();
    return ast_uop(OP_CAST, ctype, expr);
}

static Node *read_unary_expr(void) {
    Token *tok = read_token();
    if (!tok) error("premature end of input");
    if (is_ident(tok, "sizeof"))
        return get_sizeof_size(false);
    if (is_punct(tok, '(')) {
        if (is_type_keyword(peek_token()))
            return read_cast();
        Node *r = read_expr();
        expect(')');
        return r;
    }
    if (is_punct(tok, '&')) {
        Node *operand = read_expr_int(3);
        ensure_lvalue(operand);
        return ast_uop(AST_ADDR, make_ptr_type(operand->ctype), operand);
    }
    if (is_punct(tok, '!')) {
        Node *operand = read_expr_int(3);
        return ast_uop('!', ctype_int, operand);
    }
    if (is_punct(tok, '-')) {
        Node *expr = read_expr_int(3);
        return ast_binop('-', ast_inttype(ctype_int, 0), expr);
    }
    if (is_punct(tok, '~')) {
        Node *expr = read_expr_int(3);
        if (!is_inttype(expr->ctype))
            error("invalid use of ~: %s", a2s(expr));
        return ast_uop('~', expr->ctype, expr);
    }
    if (is_punct(tok, '*')) {
        Node *operand = read_expr_int(3);
        Ctype *ctype = convert_array(operand->ctype);
        if (ctype->type != CTYPE_PTR)
            error("pointer type expected, but got %s", a2s(operand));
        return ast_uop(AST_DEREF, operand->ctype->ptr, operand);
    }
    unget_token(tok);
    return read_prim();
}

static Node *read_cond_expr(Node *cond) {
    Node *then = read_expr();
    expect(':');
    Node *els = read_expr();
    return ast_ternary(then->ctype, cond, then, els);
}

static int priority(Token *tok) {
    switch (tok->punct) {
    case '[': case '.': case OP_ARROW:
        return 1;
    case OP_INC: case OP_DEC:
        return 2;
    case '*': case '/': case '%':
        return 3;
    case '+': case '-':
        return 4;
    case OP_LSH: case OP_RSH:
        return 5;
    case '<': case '>': case OP_LE: case OP_GE: case OP_NE:
        return 6;
    case OP_EQ:     return 7;
    case '&':       return 8;
    case '^':       return 9;
    case '|':       return 10;
    case OP_LOGAND: return 11;
    case OP_LOGOR:  return 12;
    case '?':       return 13;
    case '=':       return 14;
    default:        return -1;
    }
}

static Node *read_expr_int(int prec) {
    Node *node = read_unary_expr();
    if (!node) return NULL;
    for (;;) {
        Token *tok = read_token();
        if (!tok)
            return node;
        if (tok->type != TTYPE_PUNCT) {
            unget_token(tok);
            return node;
        }
        int prec2 = priority(tok);
        if (prec2 < 0 || prec <= prec2) {
            unget_token(tok);
            return node;
        }
        if (is_punct(tok, '?')) {
            node = read_cond_expr(node);
            continue;
        }
        if (is_punct(tok, '.')) {
            node = read_struct_field(node);
            continue;
        }
        if (is_punct(tok, OP_ARROW)) {
            if (node->ctype->type != CTYPE_PTR)
                error("pointer type expected, but got %s %s",
                      c2s(node->ctype), a2s(node));
            node = ast_uop(AST_DEREF, node->ctype->ptr, node);
            node = read_struct_field(node);
            continue;
        }
        if (is_punct(tok, '[')) {
            node = read_subscript_expr(node);
            continue;
        }
        if (is_punct(tok, OP_INC) || is_punct(tok, OP_DEC)) {
            ensure_lvalue(node);
            node = ast_uop(tok->punct, node->ctype, node);
            continue;
        }
        if (is_punct(tok, '='))
            ensure_lvalue(node);
        Node *rest = read_expr_int(prec2 + (is_right_assoc(tok) ? 1 : 0));
        if (!rest)
            error("second operand missing");
        if (is_punct(tok, '^') || is_punct(tok, '%') ||
            is_punct(tok, OP_LSH) || is_punct(tok, OP_RSH)) {
            ensure_inttype(node);
            ensure_inttype(rest);
        }
        node = ast_binop(tok->punct, node, rest);
    }
}

Node *read_expr(void) {
    return read_expr_int(MAX_OP_PRIO);
}

/*----------------------------------------------------------------------
 * Struct or union
 */

static Node *read_struct_field(Node *struc) {
    if (struc->ctype->type != CTYPE_STRUCT)
        error("struct expected, but got %s", a2s(struc));
    Token *name = read_token();
    if (name->type != TTYPE_IDENT)
        error("field name expected, but got %s", t2s(name));
    Ctype *field = dict_get(struc->ctype->fields, name->sval);
    return ast_struct_ref(field, struc, name->sval);
}

static char *read_struct_union_tag(void) {
    Token *tok = read_token();
    if (tok->type == TTYPE_IDENT)
        return tok->sval;
    unget_token(tok);
    return NULL;
}

static int compute_padding(int offset, int size) {
    size = MIN(size, MAX_ALIGN);
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

static Dict *read_struct_union_fields(int *rsize, bool is_struct) {
    Token *tok = read_token();
    if (!is_punct(tok, '{')) {
        unget_token(tok);
        return NULL;
    }
    int offset = 0, maxsize = 0;
    Dict *r = make_dict(NULL);
    for (;;) {
        if (!is_type_keyword(peek_token()))
            break;
        Ctype *basetype = read_decl_spec(NULL);
        if (basetype->type == CTYPE_STRUCT && is_punct(peek_token(), ';')) {
            read_token();
            squash_unnamed_struct(r, basetype, offset);
            if (is_struct)
                offset += basetype->size;
            else
                maxsize = MAX(maxsize, basetype->size);
            continue;
        }
        for (;;) {
            char *name;
            Ctype *fieldtype = read_declarator(&name, basetype, NULL, DECL_PARAM);
            ensure_not_void(fieldtype);
            if (is_struct) {
                offset += compute_padding(offset, fieldtype->size);
                fieldtype = make_struct_field_type(fieldtype, offset);
                offset += fieldtype->size;
            } else {
                maxsize = MAX(maxsize, fieldtype->size);
                fieldtype = make_struct_field_type(fieldtype, 0);
            }
            dict_put(r, name, fieldtype);
            tok = read_token();
            if (is_punct(tok, ','))
                continue;
            unget_token(tok);
            expect(';');
            break;
        }
    }
    expect('}');
    *rsize = is_struct ? offset : maxsize;
    return r;
}

static Ctype *read_struct_union_def(Dict *env, bool is_struct) {
    char *tag = read_struct_union_tag();
    Ctype *prev = tag ? dict_get(env, tag) : NULL;
    int size;
    Dict *fields = read_struct_union_fields(&size, is_struct);
    if (prev && !fields)
        return prev;
    if (prev && fields) {
        prev->fields = fields;
        prev->size = size;
        return prev;
    }
    Ctype *r = fields
        ? make_struct_type(fields, size)
        : make_struct_type(NULL, 0);
    if (tag)
        dict_put(env, tag, r);
    return r;
}

static Ctype *read_struct_def(void) {
    return read_struct_union_def(struct_defs, true);
}

static Ctype *read_union_def(void) {
    return read_struct_union_def(union_defs, false);
}

/*----------------------------------------------------------------------
 * Enum
 */

static Ctype *read_enum_def(void) {
    Token *tok = read_token();
    if (tok->type == TTYPE_IDENT)
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
        if (tok->type != TTYPE_IDENT)
            error("Identifier expected, but got %s", t2s(tok));
        char *name = tok->sval;

        tok = read_token();
        if (is_punct(tok, '='))
            val = eval_intexpr(read_expr());
        else
            unget_token(tok);

        Node *constval = ast_inttype(ctype_int, val++);
        dict_put(localenv ? localenv : globalenv, name, constval);
        tok = read_token();
        if (is_punct(tok, ','))
            continue;
        if (is_punct(tok, '}'))
            break;
        error("',' or '}' expected, but got %s", t2s(tok));
    }
    return ctype_int;
}

/*----------------------------------------------------------------------
 * Initializer
 */

static void read_decl_init_elem(List *initlist, Ctype *ctype) {
    Token *tok = peek_token();
    Node *init = read_expr();
    if (!init)
        error("expression expected, but got %s", t2s(tok));
    result_type('=', init->ctype, ctype);
    init->totype = ctype;
    tok = read_token();
    if (!is_punct(tok, ','))
        unget_token(tok);
    list_push(initlist, init);
}

static void read_decl_array_init_int(List *initlist, Ctype *ctype) {
    Token *tok = read_token();
    assert(ctype->type == CTYPE_ARRAY);
    if (ctype->ptr->type == CTYPE_CHAR && tok->type == TTYPE_STRING) {
        for (char *p = tok->sval; *p; p++) {
            Node *c = ast_inttype(ctype_char, *p);
            c->totype = ctype_char;
            list_push(initlist, c);
        }
        Node *c = ast_inttype(ctype_char, '\0');
        c->totype = ctype_char;
        list_push(initlist, c);
        return;
    }
    if (!is_punct(tok, '{'))
        error("Expected an initializer list for %s, but got %s",
              c2s(ctype), t2s(tok));
    for (;;) {
        Token *tok = read_token();
        if (is_punct(tok, '}'))
            break;
        unget_token(tok);
        read_decl_init_elem(initlist, ctype->ptr);
    }
}

static Node *read_decl_array_init_val(Ctype *ctype) {
    List *initlist = make_list();
    read_decl_array_init_int(initlist, ctype);
    Node *init = ast_init_list(initlist);

    int len = (init->type == AST_STRING)
        ? strlen(init->sval) + 1
        : list_len(init->initlist);
    if (ctype->len == -1) {
        ctype->len = len;
        ctype->size = len * ctype->ptr->size;
    } else if (ctype->len != len) {
        error("Invalid array initializer: expected %d items but got %d",
              ctype->len, len);
    }
    return init;
}

static Node *read_decl_struct_init_val(Ctype *ctype) {
    expect('{');
    List *initlist = make_list();
    for (Iter *i = list_iter(dict_values(ctype->fields)); !iter_end(i);) {
        Ctype *fieldtype = iter_next(i);
        Token *tok = read_token();
        if (is_punct(tok, '}'))
            return ast_init_list(initlist);
        if (is_punct(tok, '{')) {
            if (fieldtype->type != CTYPE_ARRAY)
                error("array expected, but got %s", c2s(fieldtype));
            unget_token(tok);
            read_decl_array_init_int(initlist, fieldtype);
            continue;
        }
        unget_token(tok);
        read_decl_init_elem(initlist, fieldtype);
    }
    expect('}');
    return ast_init_list(initlist);
}

static Node *read_decl_init(Node *var) {
    Ctype *ctype = var->ctype;
    Node *init = (ctype->type == CTYPE_ARRAY) ? read_decl_array_init_val(ctype)
        : (ctype->type == CTYPE_STRUCT) ? read_decl_struct_init_val(ctype)
        : read_expr();
    if (var->type == AST_GVAR && is_inttype(var->ctype))
        init = ast_inttype(ctype_int, eval_intexpr(init));
    return ast_decl(var, init);
}

/*----------------------------------------------------------------------
 * Declarator
 */

static void read_func_param(Ctype **rtype, char **name, bool optional) {
    int sclass;
    Ctype *basetype = read_decl_spec(&sclass);
    *rtype = read_declarator(name, basetype, NULL, optional ? DECL_PARAM_TYPEONLY : DECL_PARAM);
}

static Ctype *read_func_param_list(List *paramvars, Ctype *rettype) {
    bool typeonly = !paramvars;
    List *paramtypes = make_list();
    Token *tok = read_token();
    Token *tok2 = read_token();
    if (is_ident(tok, "void") && is_punct(tok2, ')'))
        return make_func_type(rettype, paramtypes, false);
    unget_token(tok2);
    if (is_punct(tok, ')'))
        return make_func_type(rettype, paramtypes, true);
    unget_token(tok);
    for (;;) {
        tok = read_token();
        if (is_ident(tok, "...")) {
            if (list_len(paramtypes) == 0)
                error("at least one parameter is required");
            expect(')');
            return make_func_type(rettype, paramtypes, true);
        } else
            unget_token(tok);
        Ctype *ptype;
        char *name;
        read_func_param(&ptype, &name, typeonly);
        ensure_not_void(ptype);
        if (ptype->type == CTYPE_ARRAY)
            ptype = make_ptr_type(ptype->ptr);
        list_push(paramtypes, ptype);
        if (!typeonly)
            list_push(paramvars, ast_lvar(ptype, name));
        Token *tok = read_token();
        if (is_punct(tok, ')'))
            return make_func_type(rettype, paramtypes, false);
        if (!is_punct(tok, ','))
            error("comma expected, but got %s", t2s(tok));
    }
}

static Ctype *read_direct_declarator2(Ctype *basetype, List *params) {
    Token *tok = read_token();
    if (is_punct(tok, '[')) {
        int len;
        tok = read_token();
        if (is_punct(tok, ']')) {
            len = -1;
        } else {
            unget_token(tok);
            len = eval_intexpr(read_expr());
            expect(']');
        }
        Ctype *t = read_direct_declarator2(basetype, params);
        if (t->type == CTYPE_FUNC)
            error("array of functions");
        return make_array_type(t, len);
    }
    if (is_punct(tok, '(')) {
        if (basetype->type == CTYPE_FUNC)
            error("function returning an function");
        if (basetype->type == CTYPE_ARRAY)
            error("function returning an array");
        return read_func_param_list(params, basetype);
    }
    unget_token(tok);
    return basetype;
}

static void skip_type_qualifiers(void) {
    for (;;) {
        Token *tok = read_token();
        if (is_ident(tok, "const") ||
            is_ident(tok, "volatile") ||
            is_ident(tok, "restrict"))
            continue;
        unget_token(tok);
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
        Ctype *stub = make_stub_type();
        Ctype *t = read_direct_declarator1(rname, stub, params, ctx);
        *stub = *make_ptr_type(basetype);
        return t;
    }
    if (tok->type == TTYPE_IDENT) {
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
 * Declaration specifier
 */

static Ctype *read_decl_spec(int *rsclass) {
    int sclass = 0;
    Token *tok = peek_token();
    if (!tok || tok->type != TTYPE_IDENT)
        error("internal error");

#define unused __attribute__((unused))
    bool kconst unused = false, kvolatile unused = false, kinline unused = false;
#undef unused
    Ctype *usertype = NULL, *tmp = NULL;
    enum { kvoid = 1, kchar, kint, kfloat, kdouble } type = 0;
    enum { kshort = 1, klong, kllong } size = 0;
    enum { ksigned = 1, kunsigned } sig = 0;

    for (;;) {
#define setsclass(val)                          \
        if (sclass != 0) goto err;              \
        sclass = val
#define set(var, val)                                                   \
        if (var != 0) goto err;                                         \
        var = val;                                                      \
        if (size == kshort && (type != 0 && type != kint))              \
            goto err;                                                   \
        if (size == klong && (type != 0 && type != kint && type != kdouble)) \
            goto err;                                                   \
        if (sig != 0 && (type == kvoid || type == kfloat || type == kdouble)) \
            goto err;                                                   \
        if (usertype && (type != 0 || size != 0 || sig != 0))           \
            goto err
#define _(s) (!strcmp(tok->sval, s))

        tok = read_token();
        if (!tok)
            error("premature end of input");
        if (tok->type != TTYPE_IDENT) {
            unget_token(tok);
            break;
        }
        if (_("typedef"))       { setsclass(S_TYPEDEF); }
        else if (_("extern"))   { setsclass(S_EXTERN); }
        else if (_("static"))   { setsclass(S_STATIC); }
        else if (_("auto"))     { setsclass(S_AUTO); }
        else if (_("register")) { setsclass(S_REGISTER); }
        else if (_("const"))    { kconst = 1; }
        else if (_("volatile")) { kvolatile = 1; }
        else if (_("inline"))   { kinline = 1; }
        else if (_("void"))     { set(type, kvoid); }
        else if (_("char"))     { set(type, kchar); }
        else if (_("int"))      { set(type, kint); }
        else if (_("float"))    { set(type, kfloat); }
        else if (_("double"))   { set(type, kdouble); }
        else if (_("signed"))   { set(sig, ksigned); }
        else if (_("__signed__")) { set(sig, ksigned); }
        else if (_("unsigned")) { set(sig, kunsigned); }
        else if (_("short"))    { set(size, kshort); }
        else if (_("struct"))   { set(usertype, read_struct_def()); }
        else if (_("union"))    { set(usertype, read_union_def()); }
        else if (_("enum"))     { set(usertype, read_enum_def());
        } else if ((tmp = dict_get(typedefs, tok->sval)) != NULL) {
            set(usertype, tmp);
        } else if (_("long")) {
            if (size == 0) set(size, klong);
            else if (size == klong) size = kllong;
            else goto err;
        } else {
            unget_token(tok);
            break;
        }
#undef _
#undef set
#undef setsclass
    }
    if (rsclass)
        *rsclass = sclass;
    if (usertype)
        return usertype;
    switch (type) {
    case kvoid:   return ctype_void;
    case kchar:   return make_type(CTYPE_CHAR, sig != kunsigned);
    case kfloat:  return make_type(CTYPE_FLOAT, false);
    case kdouble: return make_type(size == klong ? CTYPE_DOUBLE : CTYPE_LDOUBLE, false);
    default: break;
    }
    switch (size) {
    case kshort: return make_type(CTYPE_SHORT, sig != kunsigned);
    case klong:  return make_type(CTYPE_LONG, sig != kunsigned);
    case kllong: return make_type(CTYPE_LLONG, sig != kunsigned);
    default:     return make_type(CTYPE_INT, sig != kunsigned);
    }
    error("internal error: type: %d, size: %d", type, size);
 err:
    error("type mismatch: %s", t2s(tok));
}

/*----------------------------------------------------------------------
 * Declaration
 */

static void read_decl(List *block, MakeVarFn make_var) {
    int sclass;
    Ctype *basetype = read_decl_spec(&sclass);
    Token *tok = read_token();
    if (is_punct(tok, ';'))
        return;
    unget_token(tok);
    for (;;) {
        char *name = NULL;
        Ctype *ctype = read_declarator(&name, basetype, NULL, DECL_BODY);
        tok = read_token();
        if (is_punct(tok, '=')) {
            if (sclass == S_TYPEDEF)
                error("= after typedef");
            ensure_not_void(ctype);
            Node *var = make_var(ctype, name);
            list_push(block, read_decl_init(var));
            tok = read_token();
        } else if (sclass == S_TYPEDEF) {
            dict_put(typedefs, name, ctype);
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
            error("; or , are expected, but got %s", t2s(tok));
    }
}

/*----------------------------------------------------------------------
 * Function definition
 */

static Node *read_func_body(Ctype *functype, char *fname, List *params) {
    localenv = make_dict(localenv);
    localvars = make_list();
    current_func_type = functype;
    Node *body = read_compound_stmt();
    Node *r = ast_func(functype, fname, params, body, localvars);
    dict_put(globalenv, fname, r);
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

static Node *read_funcdef(void) {
    Ctype *basetype = read_decl_spec(NULL);
    localenv = make_dict(globalenv);
    char *name;
    List *params = make_list();
    Ctype *functype = read_declarator(&name, basetype, params, DECL_BODY);
    expect('{');
    Node *r = read_func_body(functype, name, params);
    localenv = NULL;
    return r;
}

/*----------------------------------------------------------------------
 * If
 */

static Node *read_if_stmt(void) {
    expect('(');
    Node *cond = read_expr();
    expect(')');
    Node *then = read_stmt();
    Token *tok = read_token();
    if (!tok || tok->type != TTYPE_IDENT || strcmp(tok->sval, "else")) {
        unget_token(tok);
        return ast_if(cond, then, NULL);
    }
    Node *els = read_stmt();
    return ast_if(cond, then, els);
}

/*----------------------------------------------------------------------
 * For
 */

static Node *read_opt_decl_or_stmt(void) {
    Token *tok = read_token();
    if (is_punct(tok, ';'))
        return NULL;
    unget_token(tok);
    List *list = make_list();
    read_decl_or_stmt(list);
    return list_shift(list);
}

static Node *read_opt_expr(void) {
    Token *tok = read_token();
    if (is_punct(tok, ';'))
        return NULL;
    unget_token(tok);
    Node *r = read_expr();
    expect(';');
    return r;
}

static Node *read_for_stmt(void) {
    expect('(');
    localenv = make_dict(localenv);
    Node *init = read_opt_decl_or_stmt();
    Node *cond = read_opt_expr();
    Node *step = is_punct(peek_token(), ')')
        ? NULL : read_expr();
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
    Node *cond = read_expr();
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
    if (!is_ident(tok, "while"))
        error("'while' is expected, but got %s", t2s(tok));
    expect('(');
    Node *cond = read_expr();
    expect(')');
    expect(';');
    return ast_do(cond, body);
}

/*----------------------------------------------------------------------
 * Return
 */

static Node *read_return_stmt(void) {
    Node *retval = read_expr();
    expect(';');
    return ast_return(current_func_type->rettype, retval);
}

/*----------------------------------------------------------------------
 * Statement
 */

static Node *read_stmt(void) {
    Token *tok = read_token();
    if (is_ident(tok, "if"))     return read_if_stmt();
    if (is_ident(tok, "for"))    return read_for_stmt();
    if (is_ident(tok, "while"))  return read_while_stmt();
    if (is_ident(tok, "do"))     return read_do_stmt();
    if (is_ident(tok, "return")) return read_return_stmt();
    if (is_punct(tok, '{'))      return read_compound_stmt();
    unget_token(tok);
    Node *r = read_expr();
    expect(';');
    return r;
}

static Node *read_compound_stmt(void) {
    localenv = make_dict(localenv);
    List *list = make_list();
    for (;;) {
        read_decl_or_stmt(list);
        Token *tok = read_token();
        if (is_punct(tok, '}'))
            break;
        unget_token(tok);
    }
    localenv = dict_parent(localenv);
    return ast_compound_stmt(list);
}

static void read_decl_or_stmt(List *list) {
    Token *tok = peek_token();
    if (tok == NULL)
        error("premature end of input");
    if (is_type_keyword(tok))
        read_decl(list, ast_lvar);
    else
        list_push(list, read_stmt());
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
