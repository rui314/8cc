#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

#define MAX_ARGS 6
#define MAX_OP_PRIO 16
#define MAX_ALIGN 16

Env *globalenv = &EMPTY_ENV;
List *flonums = &EMPTY_LIST;
static List *struct_defs = &EMPTY_LIST;
static List *union_defs = &EMPTY_LIST;
static Env *localenv = NULL;
static List *localvars = NULL;

static Ctype *ctype_int = &(Ctype){ CTYPE_INT, NULL, 4 };
static Ctype *ctype_char = &(Ctype){ CTYPE_CHAR, NULL, 1 };
static Ctype *ctype_float = &(Ctype){ CTYPE_FLOAT, NULL, 4 };
static Ctype *ctype_double = &(Ctype){ CTYPE_DOUBLE, NULL, 8 };

static int labelseq = 0;

static Ast *read_expr(void);
static Ctype* make_ptr_type(Ctype *ctype);
static Ctype* make_array_type(Ctype *ctype, int size);
static Ast *read_compound_stmt(void);
static Ast *read_decl_or_stmt(void);
static Ctype *result_type(char op, Ctype *a, Ctype *b);
static Ctype *convert_array(Ctype *ctype);
static Ast *read_stmt(void);
static Ctype *read_decl_int(Token **name);

static Env *make_env(Env *next) {
    Env *r = malloc(sizeof(Env));
    r->next = next;
    r->vars = make_list();
    return r;
}

static void env_append(Env *env, Ast *var) {
    assert(var->type == AST_LVAR || var->type == AST_GVAR ||
           var->type == AST_STRING);
    list_push(env->vars, var);
}

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

static Ast *ast_int(int val) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LITERAL;
    r->ctype = ctype_int;
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

static Ast *ast_char(char c) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LITERAL;
    r->ctype = ctype_char;
    r->c = c;
    return r;
}

char *make_label(void) {
    String *s = make_string();
    string_appendf(s, ".L%d", labelseq++);
    return get_cstring(s);
}

static Ast *ast_lvar(Ctype *ctype, char *name) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_LVAR;
    r->ctype = ctype;
    r->varname = name;
    env_append(localenv, r);
    if (localvars)
        list_push(localvars, r);
    return r;
}

static Ast *ast_gvar(Ctype *ctype, char *name, bool filelocal) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_GVAR;
    r->ctype = ctype;
    r->varname = name;
    r->glabel = filelocal ? make_label() : name;
    env_append(globalenv, r);
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

static Ast *ast_funcall(Ctype *ctype, char *fname, List *args) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_FUNCALL;
    r->ctype = ctype;
    r->fname = fname;
    r->args = args;
    return r;
}

static Ast *ast_func(Ctype *rettype, char *fname, List *params, Ast *body, List *localvars) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_FUNC;
    r->ctype = rettype;
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

static Ast *ast_array_init(List *arrayinit) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_ARRAY_INIT;
    r->ctype = NULL;
    r->arrayinit = arrayinit;
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

static Ast *ast_return(Ast *retval) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_RETURN;
    r->ctype = NULL;
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

static Ast *ast_struct_ref(Ast *struc, Ctype *field) {
    Ast *r = malloc(sizeof(Ast));
    r->type = AST_STRUCT_REF;
    r->ctype = field;
    r->struc = struc;
    r->field = field;
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

static Ctype* make_struct_field_type(Ctype *ctype, char *name, int offset) {
    Ctype *r = malloc(sizeof(Ctype));
    memcpy(r, ctype, sizeof(Ctype));
    r->name = name;
    r->offset = offset;
    return r;
}

static Ctype* make_struct_type(List *ctypes, char *tag, int size) {
    Ctype *r = malloc(sizeof(Ctype));
    r->type = CTYPE_STRUCT;
    r->fields = ctypes;
    r->tag = tag;
    r->size = size;
    return r;
}

static Ast *find_var(char *name) {
    for (Env *p = localenv; p; p = p->next) {
        for (Iter *i = list_iter(p->vars); !iter_end(i);) {
            Ast *v = iter_next(i);
            if (!strcmp(name, v->varname))
                return v;
        }
    }
    return NULL;
}

static void ensure_lvalue(Ast *ast) {
    switch (ast->type) {
    case AST_LVAR: case AST_GVAR: case AST_DEREF: case AST_STRUCT_REF:
        return;
    default:
        error("lvalue expected, but got %s", ast_to_string(ast));
    }
}

static void expect(char punct) {
    Token *tok = read_token();
    if (!is_punct(tok, punct))
        error("'%c' expected, but got %s", punct, token_to_string(tok));
}

static bool is_ident(Token *tok, char *s) {
    return tok->type == TTYPE_IDENT && !strcmp(tok->sval, s);
}

static bool is_right_assoc(Token *tok) {
    return tok->punct == '=';
}

static int eval_intexpr(Ast *ast) {
    switch (ast->type) {
    case AST_LITERAL:
        if (ast->ctype->type == CTYPE_INT)
            return ast->ival;
        if (ast->ctype->type == CTYPE_CHAR)
            return ast->c;
        error("Integer expression expected, but got %s", ast_to_string(ast));
    case '+': return eval_intexpr(ast->left) + eval_intexpr(ast->right);
    case '-': return eval_intexpr(ast->left) - eval_intexpr(ast->right);
    case '*': return eval_intexpr(ast->left) * eval_intexpr(ast->right);
    case '/': return eval_intexpr(ast->left) / eval_intexpr(ast->right);
    default:
        error("Integer expression expected, but got %s", ast_to_string(ast));
    }
}

static int priority(Token *tok) {
    switch (tok->punct) {
    case '[': case '.': case PUNCT_ARROW:
        return 1;
    case PUNCT_INC: case PUNCT_DEC:
        return 2;
    case '*': case '/':
        return 3;
    case '+': case '-':
        return 4;
    case '<': case '>':
        return 6;
    case '&':
        return 8;
    case '|':
        return 10;
    case PUNCT_EQ:
        return 7;
    case PUNCT_LOGAND:
        return 11;
    case PUNCT_LOGOR:
        return 12;
    case '?':
        return 13;
    case '=':
        return 14;
    default:
        return -1;
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
            error("Unexpected token: '%s'", token_to_string(tok));
    }
    if (MAX_ARGS < list_len(args))
        error("Too many arguments: %s", fname);
    return ast_funcall(ctype_int, fname, args);
}

static Ast *read_ident_or_func(char *name) {
    Token *tok = read_token();
    if (is_punct(tok, '('))
        return read_func_args(name);
    unget_token(tok);
    Ast *v = find_var(name);
    if (!v)
        error("Undefined varaible: %s", name);
    return v;
}

static bool is_int(char *p) {
    for (; *p; p++)
        if (!isdigit(*p))
            return false;
    return true;
}

static bool is_flonum(char *p) {
    for (; *p; p++)
        if (!isdigit(*p))
            break;
    if (*p++ != '.')
        return false;
    for (; *p; p++)
        if (!isdigit(*p))
            return false;
    return true;
}

static Ast *read_prim(void) {
    Token *tok = read_token();
    if (!tok) return NULL;
    switch (tok->type) {
    case TTYPE_IDENT:
        return read_ident_or_func(tok->sval);
    case TTYPE_NUMBER:
        if (is_int(tok->sval))
            return ast_int(atoi(tok->sval));
        if (is_flonum(tok->sval))
            return ast_double(atof(tok->sval));
        error("Malformed number: %s", token_to_string(tok));
    case TTYPE_CHAR:
        return ast_char(tok->c);
    case TTYPE_STRING: {
        Ast *r = ast_string(tok->sval);
        env_append(globalenv, r);
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
        if (a->type != CTYPE_INT)
            goto err;
        return b;
    }
    switch (a->type) {
    case CTYPE_VOID:
        goto err;
    case CTYPE_INT:
    case CTYPE_CHAR:
        switch (b->type) {
        case CTYPE_INT:
        case CTYPE_CHAR:
            return ctype_int;
        case CTYPE_FLOAT:
        case CTYPE_DOUBLE:
            return ctype_double;
        case CTYPE_ARRAY:
        case CTYPE_PTR:
            return b;
        }
        error("internal error");
    case CTYPE_FLOAT:
        if (b->type == CTYPE_FLOAT || b->type == CTYPE_DOUBLE)
            return ctype_double;
        goto err;
    case CTYPE_DOUBLE:
        if (b->type == CTYPE_DOUBLE)
            return ctype_double;
        goto err;
    case CTYPE_ARRAY:
        if (b->type != CTYPE_ARRAY)
            goto err;
        return result_type_int(jmpbuf, op, a->ptr, b->ptr);
    default:
        error("internal error: %s %s", ctype_to_string(a), ctype_to_string(b));
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

static Ctype *result_type(char op, Ctype *a, Ctype *b) {
    jmp_buf jmpbuf;
    if (setjmp(jmpbuf) == 0)
        return result_type_int(&jmpbuf, op, convert_array(a), convert_array(b));
    error("incompatible operands: %c: <%s> and <%s>",
          op, ctype_to_string(a), ctype_to_string(b));
}

static Ast *read_unary_expr(void) {
    Token *tok = read_token();
    if (tok->type != TTYPE_PUNCT) {
        unget_token(tok);
        return read_prim();
    }
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
    if (is_punct(tok, '*')) {
        Ast *operand = read_unary_expr();
        Ctype *ctype = convert_array(operand->ctype);
        if (ctype->type != CTYPE_PTR)
            error("pointer type expected, but got %s", ast_to_string(operand));
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

static Ctype *find_struct_field(Ctype *struc, char *name) {
    for (Iter *i = list_iter(struc->fields); !iter_end(i);) {
        Ctype *field = iter_next(i);
        if (!strcmp(field->name, name))
            return field;
    }
    return NULL;
}

static Ast *read_struct_field(Ast *struc) {
    if (struc->ctype->type != CTYPE_STRUCT)
        error("struct expected, but got %s", ast_to_string(struc));
    Token *name = read_token();
    if (name->type != TTYPE_IDENT)
        error("field name expected, but got %s", token_to_string(name));
    Ctype *field = find_struct_field(struc->ctype, name->sval);
    return ast_struct_ref(struc, field);
}

static Ast *read_expr_int(int prec) {
    Ast *ast = read_unary_expr();
    if (!ast) return NULL;
    for (;;) {
        Token *tok = read_token();
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
        if (is_punct(tok, PUNCT_ARROW)) {
            if (ast->ctype->type != CTYPE_PTR)
                error("pointer type expected, but got %s %s",
                      ctype_to_string(ast->ctype), ast_to_string(ast));
            ast = ast_uop(AST_DEREF, ast->ctype->ptr, ast);
            ast = read_struct_field(ast);
            continue;
        }
        if (is_punct(tok, '[')) {
            ast = read_subscript_expr(ast);
            continue;
        }
        if (is_punct(tok, PUNCT_INC) || is_punct(tok, PUNCT_DEC)) {
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

static Ast *read_expr(void) {
    return read_expr_int(MAX_OP_PRIO);
}

static Ctype *get_ctype(Token *tok) {
    if (!tok) return NULL;
    if (tok->type != TTYPE_IDENT)
        return NULL;
    if (!strcmp(tok->sval, "int"))    return ctype_int;
    if (!strcmp(tok->sval, "char"))   return ctype_char;
    if (!strcmp(tok->sval, "float"))  return ctype_float;
    if (!strcmp(tok->sval, "double")) return ctype_double;
    return NULL;
}

static bool is_type_keyword(Token *tok) {
    return get_ctype(tok) != NULL
        || is_ident(tok, "struct")
        || is_ident(tok, "union");
}

static Ast *read_decl_array_init_int(Ctype *ctype) {
    Token *tok = read_token();
    if (ctype->ptr->type == CTYPE_CHAR && tok->type == TTYPE_STRING)
        return ast_string(tok->sval);
    if (!is_punct(tok, '{'))
        error("Expected an initializer list for %s, but got %s",
              ctype_to_string(ctype), token_to_string(tok));
    List *initlist = make_list();
    for (;;) {
        Token *tok = read_token();
        if (is_punct(tok, '}'))
            break;
        unget_token(tok);
        Ast *init = read_expr();
        list_push(initlist, init);
        result_type('=', init->ctype, ctype->ptr);
        tok = read_token();
        if (!is_punct(tok, ','))
            unget_token(tok);
    }
    return ast_array_init(initlist);
}

static Ctype *find_struct_union_def(List *list, char *name) {
    for (Iter *i = list_iter(list); !iter_end(i);) {
        Ctype *t = iter_next(i);
        if (t->tag && !strcmp(t->tag, name))
            return t;
    }
    return NULL;
}

static char *read_struct_union_tag(void) {
    Token *tok = read_token();
    if (tok->type == TTYPE_IDENT)
        return tok->sval;
    unget_token(tok);
    return NULL;
}

static List *read_struct_union_fields(void) {
    List *r = make_list();
    expect('{');
    for (;;) {
        if (!is_type_keyword(peek_token()))
            break;
        Token *name;
        Ctype *fieldtype = read_decl_int(&name);
        list_push(r, make_struct_field_type(fieldtype, name->sval, 0));
        expect(';');
    }
    expect('}');
    return r;
}

static Ctype *read_union_def(void) {
    char *tag = read_struct_union_tag();
    Ctype *ctype = find_struct_union_def(union_defs, tag);
    if (ctype) return ctype;
    List *fields = read_struct_union_fields();
    int maxsize = 0;
    for (Iter *i = list_iter(fields); !iter_end(i);) {
        Ctype *fieldtype = iter_next(i);
        maxsize = (maxsize < fieldtype->size) ? fieldtype->size : maxsize;
    }
    Ctype *r = make_struct_type(fields, tag, maxsize);
    list_push(union_defs, r);
    return r;
}

static Ctype *read_struct_def(void) {
    char *tag = read_struct_union_tag();
    Ctype *ctype = find_struct_union_def(struct_defs, tag);
    if (ctype) return ctype;
    List *fields = read_struct_union_fields();
    int offset = 0;
    for (Iter *i = list_iter(fields); !iter_end(i);) {
        Ctype *fieldtype = iter_next(i);
        int size = (fieldtype->size < MAX_ALIGN) ? fieldtype->size : MAX_ALIGN;
        if (offset % size != 0)
            offset += size - offset % size;
        fieldtype->offset = offset;
        offset += fieldtype->size;
    }
    Ctype *r = make_struct_type(fields, tag, offset);
    list_push(struct_defs, r);
    return r;
}

static Ctype *read_decl_spec(void) {
    Token *tok = read_token();
    Ctype *ctype = is_ident(tok, "struct") ? read_struct_def()
        : is_ident(tok, "union") ? read_union_def()
        : get_ctype(tok);
    if (!ctype)
        error("Type expected, but got %s", token_to_string(tok));
    for (;;) {
        tok = read_token();
        if (!is_punct(tok, '*')) {
            unget_token(tok);
            return ctype;
        }
        ctype = make_ptr_type(ctype);
    }
}

static Ast *read_decl_init_val(Ast *var) {
    if (var->ctype->type == CTYPE_ARRAY) {
        Ast *init = read_decl_array_init_int(var->ctype);
        int len = (init->type == AST_STRING)
            ? strlen(init->sval) + 1
            : list_len(init->arrayinit);
        if (var->ctype->len == -1) {
            var->ctype->len = len;
            var->ctype->size = len * var->ctype->ptr->size;
        } else if (var->ctype->len != len) {
            error("Invalid array initializer: expected %d items but got %d",
                  var->ctype->len, len);
        }
        expect(';');
        return ast_decl(var, init);
    }
    Ast *init = read_expr();
    expect(';');
    if (var->type == AST_GVAR)
        init = ast_int(eval_intexpr(init));
    return ast_decl(var, init);
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
    Token *tok = read_token();
    if (is_punct(tok, '='))
        return read_decl_init_val(var);
    if (var->ctype->len == -1)
        error("Missing array initializer");
    unget_token(tok);
    expect(';');
    return ast_decl(var, NULL);
}

static Ctype *read_decl_int(Token **name) {
    Ctype *ctype = read_decl_spec();
    *name = read_token();
    if ((*name)->type != TTYPE_IDENT)
        error("Identifier expected, but got %s", token_to_string(*name));
    return read_array_dimensions(ctype);
}

static Ast *read_decl(void) {
    Token *varname;
    Ctype *ctype = read_decl_int(&varname);
    Ast *var = ast_lvar(ctype, varname->sval);
    return read_decl_init(var);
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
    return read_decl_or_stmt();
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
    localenv = make_env(localenv);
    Ast *init = read_opt_decl_or_stmt();
    Ast *cond = read_opt_expr();
    Ast *step = is_punct(peek_token(), ')')
        ? NULL : read_expr();
    expect(')');
    Ast *body = read_stmt();
    localenv = localenv->next;
    return ast_for(init, cond, step, body);
}

static Ast *read_return_stmt(void) {
    Ast *retval = read_expr();
    expect(';');
    return ast_return(retval);
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

static Ast *read_decl_or_stmt(void) {
    Token *tok = peek_token();
    if (!tok) return NULL;
    return is_type_keyword(tok) ? read_decl() : read_stmt();
}

static Ast *read_compound_stmt(void) {
    localenv = make_env(localenv);
    List *list = make_list();
    for (;;) {
        Ast *stmt = read_decl_or_stmt();
        if (stmt) list_push(list, stmt);
        if (!stmt) break;
        Token *tok = read_token();
        if (is_punct(tok, '}'))
            break;
        unget_token(tok);
    }
    localenv = localenv->next;
    return ast_compound_stmt(list);
}

static List *read_params(void) {
    List *params = make_list();
    Token *tok = read_token();
    if (is_punct(tok, ')'))
        return params;
    unget_token(tok);
    for (;;) {
        Ctype *ctype = read_decl_spec();
        Token *pname = read_token();
        if (pname->type != TTYPE_IDENT)
            error("Identifier expected, but got %s", token_to_string(pname));
        ctype = read_array_dimensions(ctype);
        if (ctype->type == CTYPE_ARRAY)
            ctype = make_ptr_type(ctype->ptr);
        list_push(params, ast_lvar(ctype, pname->sval));
        Token *tok = read_token();
        if (is_punct(tok, ')'))
            return params;
        if (!is_punct(tok, ','))
            error("Comma expected, but got %s", token_to_string(tok));
    }
}

static Ast *read_func_def(Ctype *rettype, char *fname) {
    expect('(');
    localenv = make_env(globalenv);
    List *params = read_params();
    expect('{');
    localenv = make_env(localenv);
    localvars = make_list();
    Ast *body = read_compound_stmt();
    Ast *r = ast_func(rettype, fname, params, body, localvars);
    localvars = NULL;
    return r;
}

static Ast *read_decl_or_func_def(void) {
    Token *tok = peek_token();
    if (!tok) return NULL;
    Ctype *ctype = read_decl_spec();
    Token *name = read_token();
    if (name->type != TTYPE_IDENT)
        error("Identifier expected, but got %s", token_to_string(name));
    ctype = read_array_dimensions(ctype);
    tok = peek_token();
    if (is_punct(tok, '=') || ctype->type == CTYPE_ARRAY) {
        Ast *var = ast_gvar(ctype, name->sval, false);
        return read_decl_init(var);
    }
    if (is_punct(tok, '('))
        return read_func_def(ctype, name->sval);
    if (is_punct(tok, ';')) {
        read_token();
        Ast *var = ast_gvar(ctype, name->sval, false);
        return ast_decl(var, NULL);
    }
    error("Don't know how to handle %s", token_to_string(tok));
}

List *read_toplevels(void) {
    List *r = make_list();
    for (;;) {
        Ast *ast = read_decl_or_func_def();
        if (!ast) return r;
        list_push(r, ast);
    }
}
