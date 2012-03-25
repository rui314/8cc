#include <ctype.h>
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

#define MAX_ARGS 6
#define MAX_OP_PRIO 16
#define MAX_ALIGN 16

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
static Ctype *ctype_ulong = &(Ctype){ CTYPE_LONG, 8, false };

static int labelseq = 0;

typedef Ast *MakeVarFn(Ctype *ctype, char *name);

static Ctype* make_ptr_type(Ctype *ctype);
static Ctype* make_array_type(Ctype *ctype, int size);
static Ast *read_compound_stmt(void);
static void read_decl_or_stmt(List *list);
static Ctype *convert_array(Ctype *ctype);
static Ast *read_stmt(void);
static bool is_type_keyword(Token *tok);
static Ast *read_unary_expr(void);
static Ctype *read_func_param_list(List *rparams, Ctype *rettype);
static Ast *read_decl_init_val(Ctype *ctype);
static void read_func_param(Ctype **rtype, char **name, bool optional);
static void read_decl(List *toplevel, MakeVarFn make_var);
static Ctype *read_declarator(char **name, Ctype *basetype, List *params, int ctx);
static void read_decl_spec(Ctype **rtype, int *sclass);

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

static Ast *ast_uop(int type, Ctype *ctype, Ast *operand) {
    Ast *r = malloc(sizeof(Ast));
    r->type = type;
    r->ctype = ctype;
    r->operand = operand;
    return r;
}

static Ast *ast_binop(int type, Ast *left, Ast *right) {
    Ast *r = malloc(sizeof(Ast));
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

static Ast *ast_inttype(Ctype *ctype, long val) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LITERAL;
    r->ctype = ctype;
    r->ival = val;
    return r;
}

static Ast *ast_double(double val) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LITERAL;
    r->ctype = ctype_double;
    r->fval = val;
    list_push(flonums, r);
    return r;
}

char *make_label(void) {
    return format(".L%d", labelseq++);
}

static Ast *ast_lvar(Ctype *ctype, char *name) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LVAR;
    r->ctype = ctype;
    r->varname = name;
    if (localenv)
        dict_put(localenv, name, r);
    if (localvars)
        list_push(localvars, r);
    return r;
}

static Ast *ast_gvar(Ctype *ctype, char *name) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_GVAR;
    r->ctype = ctype;
    r->varname = name;
    r->glabel = name;
    dict_put(globalenv, name, r);
    return r;
}

static Ast *ast_string(char *str) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_STRING;
    r->ctype = make_array_type(ctype_char, strlen(str) + 1);
    r->sval = str;
    r->slabel = make_label();
    return r;
}

static Ast *ast_funcall(Ctype *ctype, char *fname, List *args, List *paramtypes) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_FUNCALL;
    r->ctype = ctype;
    r->fname = fname;
    r->args = args;
    r->paramtypes = paramtypes;
    return r;
}

static Ast *ast_func(Ctype *ctype, char *fname, List *params, Ast *body, List *localvars) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_FUNC;
    r->ctype = ctype;
    r->fname = fname;
    r->params = params;
    r->localvars = localvars;
    r->body = body;
    return r;
}

static Ast *ast_decl(Ast *var, Ast *init) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_DECL;
    r->ctype = NULL;
    r->declvar = var;
    r->declinit = init;
    return r;
}

static Ast *ast_init_list(List *initlist) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_INIT_LIST;
    r->ctype = NULL;
    r->initlist = initlist;
    return r;
}

static Ast *ast_if(Ast *cond, Ast *then, Ast *els) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_IF;
    r->ctype = NULL;
    r->cond = cond;
    r->then = then;
    r->els = els;
    return r;
}

static Ast *ast_ternary(Ctype *ctype, Ast *cond, Ast *then, Ast *els) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_TERNARY;
    r->ctype = ctype;
    r->cond = cond;
    r->then = then;
    r->els = els;
    return r;
}

static Ast *ast_for(Ast *init, Ast *cond, Ast *step, Ast *body) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_FOR;
    r->ctype = NULL;
    r->forinit = init;
    r->forcond = cond;
    r->forstep = step;
    r->forstep = step;
    r->forbody = body;
    return r;
}

static Ast *ast_return(Ctype *rettype, Ast *retval) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_RETURN;
    r->ctype = rettype;
    r->retval = retval;
    return r;
}

static Ast *ast_compound_stmt(List *stmts) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_COMPOUND_STMT;
    r->ctype = NULL;
    r->stmts = stmts;
    return r;
}

static Ast *ast_struct_ref(Ctype *ctype, Ast *struc, char *name) {
    Ast *r = malloc(sizeof(Ast));
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

bool is_inttype(Ctype *ctype) {
    return ctype->type == CTYPE_CHAR || ctype->type == CTYPE_SHORT ||
        ctype->type == CTYPE_INT || ctype->type == CTYPE_LONG || ctype->type == CTYPE_LLONG;
}

bool is_flotype(Ctype *ctype) {
    return ctype->type == CTYPE_FLOAT || ctype->type == CTYPE_DOUBLE ||
        ctype->type == CTYPE_LDOUBLE;
}

static void ensure_lvalue(Ast *ast) {
    switch (ast->type) {
    case AST_LVAR: case AST_GVAR: case AST_DEREF: case AST_STRUCT_REF:
        return;
    default:
        error("lvalue expected, but got %s", a2s(ast));
    }
}

static void expect(char punct) {
    Token *tok = read_token();
    if (!is_punct(tok, punct))
        error("'%c' expected, but got %s", punct, t2s(tok));
}

bool is_ident(Token *tok, char *s) {
    return tok->type == TTYPE_IDENT && !strcmp(tok->sval, s);
}

static bool is_right_assoc(Token *tok) {
    return tok->punct == '=';
}

int eval_intexpr(Ast *ast) {
    switch (ast->type) {
    case AST_LITERAL:
        if (is_inttype(ast->ctype))
            return ast->ival;
        error("Integer expression expected, but got %s", a2s(ast));
    case '!': return !eval_intexpr(ast->operand);
#define L (eval_intexpr(ast->left))
#define R (eval_intexpr(ast->right))
    case '+': return L + R;
    case '-': return L - R;
    case '*': return L * R;
    case '/': return L / R;
    case '<': return L < R;
    case '>': return L > R;
    case OP_EQ: return L == R;
    case OP_GE: return L >= R;
    case OP_LE: return L <= R;
    case OP_NE: return L != R;
    case OP_LOGAND: return L && R;
    case OP_LOGOR:  return L || R;
#undef L
#undef R
    default:
        error("Integer expression expected, but got %s", a2s(ast));
    }
}

static int priority(Token *tok) {
    switch (tok->punct) {
    case '[': case '.': case OP_ARROW:
        return 1;
    case OP_INC: case OP_DEC:
        return 2;
    case '*': case '/':
        return 3;
    case '+': case '-':
        return 4;
    case '<': case '>': case OP_LE: case OP_GE: case OP_NE:
        return 6;
    case '&':
        return 8;
    case '|':
        return 10;
    case OP_EQ:
        return 7;
    case OP_LOGAND:
        return 11;
    case OP_LOGOR:
        return 12;
    case '?':
        return 13;
    case '=':
        return 14;
    default:
        return -1;
    }
}

static List *param_types(List *params) {
    List *r = make_list();
    for (Iter *i = list_iter(params); !iter_end(i);)
        list_push(r, ((Ast *)iter_next(i))->ctype);
    return r;
}

static void function_type_check(char *fname, List *params, List *args) {
    if (list_len(args) < list_len(params))
        error("Too few arguments: %s", fname);
    for (Iter *i = list_iter(params), *j = list_iter(args); !iter_end(j);) {
        Ctype *param = iter_next(i);
        Ctype *arg = iter_next(j);
        if (param)
            result_type('=', param, arg);
        else
            result_type('=', arg, ctype_int);
    }
}

static Ast *read_func_args(char *fname) {
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
    Ast *func = dict_get(localenv, fname);
    if (func) {
        Ctype *t = func->ctype;
        if (t->type != CTYPE_FUNC)
            error("%s is not a function, but %s", fname, c2s(t));
        assert(t->params);
        function_type_check(fname, t->params, param_types(args));
        return ast_funcall(t->rettype, fname, args, t->params);
    }
    return ast_funcall(ctype_int, fname, args, make_list());
}

static Ast *read_ident_or_func(char *name) {
    Token *tok = read_token();
    if (is_punct(tok, '('))
        return read_func_args(name);
    unget_token(tok);
    Ast *v = dict_get(localenv, name);
    if (!v)
        error("Undefined varaible: %s", name);
    return v;
}

static Ast *read_number(char *s) {
    assert(s[0]);
    char *p = s;
    int base = 10;
    if (*p == '0') {
        p++;
        if (*p == 'x' || *p == 'X') {
            base = 16;
            p++;
        } else if (isdigit(*p)) {
            base = 8;
        }
    }
    char *start = p;
    while (isdigit(*p) || (base == 16 && ((('a' <= *p) && ('f' <= *p)) || (('A' <= *p) && ('F' <= *p)))))
        p++;
    if (*p == '.') {
        if (base != 10)
            error("malformed number: %s", s);
        p++;
        while (isdigit(*p))
            p++;
        if (*p != '\0')
            error("malformed number: %s", s);
        char *end = p - 1;
        assert(start != end);
        return ast_double(atof(start));
    }
    if (!strcasecmp(p, "l")) {
        return ast_inttype(ctype_long, strtol(start, NULL, base));
    } else if (!strcasecmp(p, "ul") || !strcasecmp(p, "lu")) {
        return ast_inttype(ctype_ulong, strtoul(start, NULL, base));
    } else {
        if (*p != '\0')
            error("malformed number: %s %c", s);
        long val = strtol(start, NULL, base);
        if (val & ~(long)UINT_MAX)
            return ast_inttype(ctype_long, val);
        return ast_inttype(ctype_int, val);
    }
}

static Ast *read_prim(void) {
    Token *tok = read_token();
    if (!tok) return NULL;
    switch (tok->type) {
    case TTYPE_IDENT:
        return read_ident_or_func(tok->sval);
    case TTYPE_NUMBER:
        return read_number(tok->sval);
    case TTYPE_CHAR:
        return ast_inttype(ctype_char, tok->c);
    case TTYPE_STRING: {
        Ast *r = ast_string(tok->sval);
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

#define swap(a, b)                              \
    { typeof(a) tmp = b; b = a; a = tmp; }

static Ctype *result_type_int(jmp_buf *jmpbuf, char op, Ctype *a, Ctype *b) {
    if (a->type > b->type)
        swap(a, b);
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

static Ast *read_subscript_expr(Ast *ast) {
    Ast *sub = read_expr();
    expect(']');
    Ast *t = ast_binop('+', ast, sub);
    return ast_uop(AST_DEREF, t->ctype->ptr, t);
}

static Ctype *convert_array(Ctype *ctype) {
    if (ctype->type != CTYPE_ARRAY)
        return ctype;
    return make_ptr_type(ctype->ptr);
}

Ctype *result_type(char op, Ctype *a, Ctype *b) {
    jmp_buf jmpbuf;
    if (setjmp(jmpbuf) == 0)
        return result_type_int(&jmpbuf, op, convert_array(a), convert_array(b));
    error("incompatible operands: %c: <%s> and <%s>",
          op, c2s(a), c2s(b));
}

static Ast *get_sizeof_size(bool allow_typename) {
    Token *tok = read_token();
    if (allow_typename && is_type_keyword(tok)) {
        unget_token(tok);
        Ctype *ctype;
        read_func_param(&ctype, NULL, true);
        return ast_inttype(ctype_long, ctype->size);
    }
    if (is_punct(tok, '(')) {
        Ast *r = get_sizeof_size(true);
        expect(')');
        return r;
    }
    unget_token(tok);
    Ast *expr = read_unary_expr();
    if (expr->ctype->size == 0)
        error("invalid operand for sizeof(): %s type=%s size=%d", a2s(expr), c2s(expr->ctype), expr->ctype->size);
    return ast_inttype(ctype_long, expr->ctype->size);
}

static Ast *read_unary_expr(void) {
    Token *tok = read_token();
    if (!tok) error("premature end of input");
    if (is_ident(tok, "sizeof"))
        return get_sizeof_size(false);
    if (is_punct(tok, '(')) {
        Ast *r = read_expr();
        expect(')');
        return r;
    }
    if (is_punct(tok, '&')) {
        Ast *operand = read_unary_expr();
        ensure_lvalue(operand);
        return ast_uop(AST_ADDR, make_ptr_type(operand->ctype), operand);
    }
    if (is_punct(tok, '!')) {
        Ast *operand = read_unary_expr();
        return ast_uop('!', ctype_int, operand);
    }
    if (is_punct(tok, '-')) {
        Ast *expr = read_expr();
        return ast_binop('-', ast_inttype(ctype_int, 0), expr);
    }
    if (is_punct(tok, '*')) {
        Ast *operand = read_unary_expr();
        Ctype *ctype = convert_array(operand->ctype);
        if (ctype->type != CTYPE_PTR)
            error("pointer type expected, but got %s", a2s(operand));
        return ast_uop(AST_DEREF, operand->ctype->ptr, operand);
    }
    unget_token(tok);
    return read_prim();
}

static Ast *read_cond_expr(Ast *cond) {
    Ast *then = read_expr();
    expect(':');
    Ast *els = read_expr();
    return ast_ternary(then->ctype, cond, then, els);
}

static Ast *read_struct_field(Ast *struc) {
    if (struc->ctype->type != CTYPE_STRUCT)
        error("struct expected, but got %s", a2s(struc));
    Token *name = read_token();
    if (name->type != TTYPE_IDENT)
        error("field name expected, but got %s", t2s(name));
    Ctype *field = dict_get(struc->ctype->fields, name->sval);
    return ast_struct_ref(field, struc, name->sval);
}

static Ast *read_expr_int(int prec) {
    Ast *ast = read_unary_expr();
    if (!ast) return NULL;
    for (;;) {
        Token *tok = read_token();
        if (!tok)
            return ast;
        if (tok->type != TTYPE_PUNCT) {
            unget_token(tok);
            return ast;
        }
        int prec2 = priority(tok);
        if (prec2 < 0 || prec <= prec2) {
            unget_token(tok);
            return ast;
        }
        if (is_punct(tok, '?')) {
            ast = read_cond_expr(ast);
            continue;
        }
        if (is_punct(tok, '.')) {
            ast = read_struct_field(ast);
            continue;
        }
        if (is_punct(tok, OP_ARROW)) {
            if (ast->ctype->type != CTYPE_PTR)
                error("pointer type expected, but got %s %s",
                      c2s(ast->ctype), a2s(ast));
            ast = ast_uop(AST_DEREF, ast->ctype->ptr, ast);
            ast = read_struct_field(ast);
            continue;
        }
        if (is_punct(tok, '[')) {
            ast = read_subscript_expr(ast);
            continue;
        }
        if (is_punct(tok, OP_INC) || is_punct(tok, OP_DEC)) {
            ensure_lvalue(ast);
            ast = ast_uop(tok->punct, ast->ctype, ast);
            continue;
        }
        if (is_punct(tok, '='))
            ensure_lvalue(ast);
        Ast *rest = read_expr_int(prec2 + (is_right_assoc(tok) ? 1 : 0));
        if (!rest)
            error("second operand missing");
        ast = ast_binop(tok->punct, ast, rest);
    }
}

Ast *read_expr(void) {
    return read_expr_int(MAX_OP_PRIO);
}

static bool is_type_keyword(Token *tok) {
    if (tok->type != TTYPE_IDENT)
        return false;
    char *keyword[] = {
        "char", "short", "int", "long", "float", "double", "struct",
        "union", "signed", "unsigned", "enum", "void", "typedef", "extern",
        "static", "auto", "register", "const", "volatile", "inline"
    };
    for (int i = 0; i < sizeof(keyword) / sizeof(*keyword); i++)
        if (!strcmp(keyword[i], tok->sval))
            return true;
    return dict_get(typedefs, tok->sval);
}

static void read_decl_init_elem(List *initlist, Ctype *ctype) {
    Token *tok = peek_token();
    Ast *init = read_expr();
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
            Ast *c = ast_inttype(ctype_char, *p);
            c->totype = ctype_char;
            list_push(initlist, c);
        }
        Ast *c = ast_inttype(ctype_char, '\0');
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

static char *read_struct_union_tag(void) {
    Token *tok = read_token();
    if (tok->type == TTYPE_IDENT)
        return tok->sval;
    unget_token(tok);
    return NULL;
}

static Dict *read_struct_union_fields(void) {
    Token *tok = read_token();
    if (!is_punct(tok, '{')) {
        unget_token(tok);
        return NULL;
    }
    Dict *r = make_dict(NULL);
    for (;;) {
        if (!is_type_keyword(peek_token()))
            break;
        Ctype *basetype;
        int dummy;
        read_decl_spec(&basetype, &dummy);
        for (;;) {
            char *name;
            Ctype *fieldtype = read_declarator(&name, basetype, NULL, DECL_PARAM);
            dict_put(r, name, make_struct_field_type(fieldtype, 0));
            tok = read_token();
            if (is_punct(tok, ','))
                continue;
            unget_token(tok);
            expect(';');
            break;
        }
    }
    expect('}');
    return r;
}

static int compute_union_size(Dict *fields) {
    int maxsize = 0;
    for (Iter *i = list_iter(dict_values(fields)); !iter_end(i);) {
        Ctype *fieldtype = iter_next(i);
        maxsize = (maxsize < fieldtype->size) ? fieldtype->size : maxsize;
    }
    return maxsize;
}

static int compute_struct_size(Dict *fields) {
    int offset = 0;
    for (Iter *i = list_iter(dict_values(fields)); !iter_end(i);) {
        Ctype *fieldtype = iter_next(i);
        int size = (fieldtype->size < MAX_ALIGN) ? fieldtype->size : MAX_ALIGN;
        if (offset % size != 0)
            offset += size - offset % size;
        fieldtype->offset = offset;
        offset += fieldtype->size;
    }
    return offset;
}

static Ctype *read_struct_union_def(Dict *env, int (*compute_size)(Dict *)) {
    char *tag = read_struct_union_tag();
    Ctype *prev = tag ? dict_get(env, tag) : NULL;
    Dict *fields = read_struct_union_fields();
    if (prev && !fields)
        return prev;
    if (prev && fields) {
        prev->fields = fields;
        prev->size = compute_size(fields);
        return prev;
    }
    Ctype *r = fields
        ? make_struct_type(fields, compute_size(fields))
        : make_struct_type(NULL, 0);
    if (tag)
        dict_put(env, tag, r);
    return r;
}

static Ctype *read_struct_def(void) {
    return read_struct_union_def(struct_defs, compute_struct_size);
}

static Ctype *read_union_def(void) {
    return read_struct_union_def(union_defs, compute_union_size);
}

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

        Ast *constval = ast_inttype(ctype_int, val++);
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
        if (is_ident(tok, "const") || is_ident(tok, "volatile"))
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

static void read_decl_spec(Ctype **rtype, int *sclass) {
    *rtype = NULL;
    *sclass = 0;
    Token *tok = peek_token();
    if (!tok || tok->type != TTYPE_IDENT)
        return;

#define unused __attribute__((unused))
    bool kconst unused = false, kvolatile unused = false, kinline unused = false;
#undef unused
    Ctype *usertype = NULL, *tmp = NULL;
    enum { kvoid = 1, kchar, kint, kfloat, kdouble } type = 0;
    enum { kshort = 1, klong, kllong } size = 0;
    enum { ksigned = 1, kunsigned } sig = 0;

    for (;;) {
#define setsclass(val)                          \
        if (*sclass != 0) goto err;             \
        *sclass = val
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
    if (usertype) {
        *rtype = usertype;
        return;
    }
    switch (type) {
    case kchar:   *rtype = make_type(CTYPE_CHAR, sig != kunsigned); return;
    case kfloat:  *rtype = make_type(CTYPE_FLOAT, false); return;
    case kdouble: *rtype = make_type(size == klong ? CTYPE_DOUBLE : CTYPE_LDOUBLE, false); return;
    default: break;
    }
    switch (size) {
    case kshort: *rtype = make_type(CTYPE_SHORT, sig != kunsigned); return;
    case klong:  *rtype = make_type(CTYPE_LONG, sig != kunsigned); return;
    case kllong: *rtype = make_type(CTYPE_LLONG, sig != kunsigned); return;
    default:     *rtype = make_type(CTYPE_INT, sig != kunsigned); return;
    }
    error("internal error: type: %d, size: %d", type, size);
 err:
    error("type mismatch: %s", t2s(tok));
}

static Ast *read_decl_array_init_val(Ctype *ctype) {
    List *initlist = make_list();
    read_decl_array_init_int(initlist, ctype);
    Ast *init = ast_init_list(initlist);

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

static Ast *read_decl_struct_init_val(Ctype *ctype) {
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

static Ast *read_decl_init_val(Ctype *ctype) {
    return (ctype->type == CTYPE_ARRAY) ? read_decl_array_init_val(ctype)
        : (ctype->type == CTYPE_STRUCT) ? read_decl_struct_init_val(ctype)
        : read_expr();
}

static Ctype *read_array_dimensions_int(Ctype *basetype) {
    Token *tok = read_token();
    if (!is_punct(tok, '[')) {
        unget_token(tok);
        return NULL;
    }
    int dim = -1;
    if (!is_punct(peek_token(), ']')) {
        Ast *size = read_expr();
        dim = eval_intexpr(size);
    }
    expect(']');
    Ctype *sub = read_array_dimensions_int(basetype);
    if (sub) {
        if (sub->len == -1 && dim == -1)
            error("Array size is not specified");
        return make_array_type(sub, dim);
    }
    return make_array_type(basetype, dim);
}

static Ctype *read_array_dimensions(Ctype *basetype) {
    Ctype *ctype = read_array_dimensions_int(basetype);
    return ctype ? ctype : basetype;
}

static Ast *read_decl_init(Ast *var) {
    Ast *init = read_decl_init_val(var->ctype);
    if (var->type == AST_GVAR && is_inttype(var->ctype))
        init = ast_inttype(ctype_int, eval_intexpr(init));
    return ast_decl(var, init);
}

static void read_func_param(Ctype **rtype, char **name, bool optional) {
    Ctype *basetype;
    int sclass;
    read_decl_spec(&basetype, &sclass);
    basetype = read_declarator(name, basetype, NULL, optional ? DECL_PARAM_TYPEONLY : DECL_PARAM);
    *rtype = read_array_dimensions(basetype);
}

static Ast *read_if_stmt(void) {
    expect('(');
    Ast *cond = read_expr();
    expect(')');
    Ast *then = read_stmt();
    Token *tok = read_token();
    if (!tok || tok->type != TTYPE_IDENT || strcmp(tok->sval, "else")) {
        unget_token(tok);
        return ast_if(cond, then, NULL);
    }
    Ast *els = read_stmt();
    return ast_if(cond, then, els);
}

static Ast *read_opt_decl_or_stmt(void) {
    Token *tok = read_token();
    if (is_punct(tok, ';'))
        return NULL;
    unget_token(tok);
    List *list = make_list();
    read_decl_or_stmt(list);
    return list_shift(list);
}

static Ast *read_opt_expr(void) {
    Token *tok = read_token();
    if (is_punct(tok, ';'))
        return NULL;
    unget_token(tok);
    Ast *r = read_expr();
    expect(';');
    return r;
}

static Ast *read_for_stmt(void) {
    expect('(');
    localenv = make_dict(localenv);
    Ast *init = read_opt_decl_or_stmt();
    Ast *cond = read_opt_expr();
    Ast *step = is_punct(peek_token(), ')')
        ? NULL : read_expr();
    expect(')');
    Ast *body = read_stmt();
    localenv = dict_parent(localenv);
    return ast_for(init, cond, step, body);
}

static Ast *read_return_stmt(void) {
    Ast *retval = read_expr();
    expect(';');
    return ast_return(current_func_type->rettype, retval);
}

static Ast *read_stmt(void) {
    Token *tok = read_token();
    if (is_ident(tok, "if"))     return read_if_stmt();
    if (is_ident(tok, "for"))    return read_for_stmt();
    if (is_ident(tok, "return")) return read_return_stmt();
    if (is_punct(tok, '{'))      return read_compound_stmt();
    unget_token(tok);
    Ast *r = read_expr();
    expect(';');
    return r;
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

static Ast *read_compound_stmt(void) {
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

static Ctype *read_func_param_list(List *paramvars, Ctype *rettype) {
    bool typeonly = !paramvars;
    List *paramtypes = make_list();
    Token *tok = read_token();
    if (is_punct(tok, ')'))
        return make_func_type(rettype, paramtypes, false);
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

static Ast *read_func_body(Ctype *functype, char *fname, List *params) {
    localenv = make_dict(localenv);
    localvars = make_list();
    current_func_type = functype;
    Ast *body = read_compound_stmt();
    Ast *r = ast_func(functype, fname, params, body, localvars);
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

static Ast *read_funcdef(void) {
    Ctype *basetype;
    int sclass;
    char *name;
    List *params = make_list();
    read_decl_spec(&basetype, &sclass);
    localenv = make_dict(globalenv);
    Ctype *functype = read_declarator(&name, basetype, params, DECL_BODY);
    expect('{');
    Ast *r = read_func_body(functype, name, params);
    localenv = NULL;
    return r;
}

static void read_decl(List *block, MakeVarFn make_var) {
    Ctype *basetype;
    int sclass;
    read_decl_spec(&basetype, &sclass);
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
            Ast *var = make_var(ctype, name);
            list_push(block, read_decl_init(var));
            tok = read_token();
        } else if (sclass == S_TYPEDEF) {
            dict_put(typedefs, name, ctype);
        } else if (ctype->type == CTYPE_FUNC) {
            make_var(ctype, name);
        } else {
            Ast *var = make_var(ctype, name);
            if (sclass != S_EXTERN)
                list_push(block, ast_decl(var, NULL));
        }
        if (is_punct(tok, ';'))
            return;
        if (!is_punct(tok, ','))
            error("; or , are expected, but got %s", t2s(tok));
    }
}

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
