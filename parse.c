#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "8cc.h"

#define MAX_ARGS 6
#define MAX_OP_PRIO 16

Env *globalenv = &EMPTY_ENV;
static Env *localenv = NULL;
static List *localvars = NULL;
static Ctype *ctype_int = &(Ctype){ CTYPE_INT, NULL };
static Ctype *ctype_char = &(Ctype){ CTYPE_CHAR, NULL };

static int labelseq = 0;

static Ast *read_expr(void);
static Ctype* make_ptr_type(Ctype *ctype);
static Ctype* make_array_type(Ctype *ctype, int size);
static void ast_to_string_int(String *buf, Ast *ast);
static Ast *read_compound_stmt(void);
static Ast *read_decl_or_stmt(void);
static Ctype *result_type(char op, Ctype *a, Ctype *b);
static Ctype *convert_array(Ctype *ctype);
static Ast *read_stmt(void);

static Env *make_env(Env *next) {
  Env *r = malloc(sizeof(Env));
  r->next = next;
  r->vars = make_list();
  return r;
}

static void env_append(Env *env, Ast *var) {
  assert(var->type == AST_LVAR || var->type == AST_GVAR ||
         var->type == AST_STRING);
  list_append(env->vars, var);
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
    list_append(localvars, r);
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

static Ctype* make_ptr_type(Ctype *ctype) {
  Ctype *r = malloc(sizeof(Ctype));
  r->type = CTYPE_PTR;
  r->ptr = ctype;
  return r;
}

static Ctype* make_array_type(Ctype *ctype, int size) {
  Ctype *r = malloc(sizeof(Ctype));
  r->type = CTYPE_ARRAY;
  r->ptr = ctype;
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
    case AST_LVAR: case AST_GVAR: case AST_DEREF:
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

static bool is_right_assoc(Token *tok) {
  return tok->punct == '=';
}

static int priority(Token *tok) {
  switch (tok->punct) {
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
    list_append(args, read_expr());
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

static Ast *read_prim(void) {
  Token *tok = read_token();
  if (!tok) return NULL;
  switch (tok->type) {
    case TTYPE_IDENT:
      return read_ident_or_func(tok->sval);
    case TTYPE_INT:
      return ast_int(tok->ival);
    case TTYPE_CHAR:
      return ast_char(tok->c);
    case TTYPE_STRING: {
      Ast *r = ast_string(tok->sval);
      env_append(globalenv, r);
      return r;
    }
    case TTYPE_PUNCT:
      error("unexpected character: '%c'", tok->punct);
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
        case CTYPE_ARRAY:
        case CTYPE_PTR:
          return b;
      }
      error("internal error");
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

static Ast *read_postfix_expr(void) {
  Ast *r = read_prim();
  for (;;) {
    Token *tok = read_token();
    if (!tok)
      return r;
    if (is_punct(tok, '[')) {
      r = read_subscript_expr(r);
    } else if (is_punct(tok, PUNCT_INC) || is_punct(tok, PUNCT_DEC)) {
      ensure_lvalue(r);
      r = ast_uop(tok->punct, r->ctype, r);
    } else {
      unget_token(tok);
      return r;
    }
  }
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
    return read_postfix_expr();
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
  Ast *then = read_unary_expr();
  expect(':');
  Ast *els = read_unary_expr();
  return ast_ternary(then->ctype, cond, then, els);
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
    if (is_punct(tok, '='))
      ensure_lvalue(ast);
    Ast *rest = read_expr_int(prec2 + (is_right_assoc(tok) ? 1 : 0));
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
  if (!strcmp(tok->sval, "int"))
    return ctype_int;
  if (!strcmp(tok->sval, "char"))
    return ctype_char;
  return NULL;
}

static bool is_type_keyword(Token *tok) {
  return get_ctype(tok) != NULL;
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
    list_append(initlist, init);
    result_type('=', init->ctype, ctype->ptr);
    tok = read_token();
    if (!is_punct(tok, ','))
      unget_token(tok);
  }
  return ast_array_init(initlist);
}

static Ctype *read_decl_spec(void) {
  Token *tok = read_token();
  Ctype *ctype = get_ctype(tok);
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

static void check_intexp(Ast *ast) {
  if (ast->type != AST_LITERAL || ast->ctype->type != CTYPE_INT)
    error("Integer expected, but got %s", ast_to_string(ast));
}

static Ast *read_decl_init_val(Ast *var) {
  if (var->ctype->type == CTYPE_ARRAY) {
    Ast *init = read_decl_array_init_int(var->ctype);
    int len = (init->type == AST_STRING)
        ? strlen(init->sval) + 1
        : list_len(init->arrayinit);
    if (var->ctype->size == -1) {
      var->ctype->size = len;
    } else if (var->ctype->size != len) {
      error("Invalid array initializer: expected %d items but got %d",
            var->ctype->size, len);
    }
    expect(';');
    return ast_decl(var, init);
  }
  Ast *init = read_expr();
  expect(';');
  if (var->type == AST_GVAR)
    check_intexp(init);
  return ast_decl(var, init);
}

static Ctype *read_array_dimensions_int(void) {
  Token *tok = read_token();
  if (!is_punct(tok, '[')) {
    unget_token(tok);
    return NULL;
  }
  int dim = -1;
  tok = peek_token();
  if (!is_punct(tok, ']')) {
    Ast *size = read_expr();
    check_intexp(size);
    dim = size->ival;
  }
  expect(']');
  Ctype *sub = read_array_dimensions_int();
  if (sub) {
    if (sub->size == -1 && dim == -1)
      error("Array size is not specified");
    return make_array_type(sub, dim);
  }
  return make_array_type(NULL, dim);
}

static Ctype *read_array_dimensions(Ctype *basetype) {
  Ctype *ctype = read_array_dimensions_int();
  if (!ctype)
    return basetype;
  Ctype *p = ctype;
  for (; p->ptr; p = p->ptr);
  p->ptr = basetype;
  return ctype;
}

static Ast *read_decl_init(Ast *var) {
  Token *tok = read_token();
  if (is_punct(tok, '='))
    return read_decl_init_val(var);
  unget_token(tok);
  expect(';');
  return ast_decl(var, NULL);
}

static Ast *read_decl(void) {
  Ctype *ctype = read_decl_spec();
  Token *varname = read_token();
  if (varname->type != TTYPE_IDENT)
    error("Identifier expected, but got %s", token_to_string(varname));
  ctype = read_array_dimensions(ctype);
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

static bool is_ident(Token *tok, char *s) {
  return tok->type == TTYPE_IDENT && !strcmp(tok->sval, s);
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
    if (stmt) list_append(list, stmt);
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
    list_append(params, ast_lvar(ctype, pname->sval));
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

List *read_func_list(void) {
  List *r = make_list();
  for (;;) {
    Ast *ast = read_decl_or_func_def();
    if (!ast) return r;
    list_append(r, ast);
  }
}

char *ctype_to_string(Ctype *ctype) {
  if (!ctype)
    return "(nil)";
  switch (ctype->type) {
    case CTYPE_VOID: return "void";
    case CTYPE_INT:  return "int";
    case CTYPE_CHAR: return "char";
    case CTYPE_PTR: {
      String *s = make_string();
      string_appendf(s, "*%s", ctype_to_string(ctype->ptr));
      return get_cstring(s);
    }
    case CTYPE_ARRAY: {
      String *s = make_string();
      string_appendf(s, "[%d]%s", ctype->size, ctype_to_string(ctype->ptr));
      return get_cstring(s);
    }
    default: error("Unknown ctype: %d", ctype);
  }
}

static void uop_to_string(String *buf, char *op, Ast *ast) {
  string_appendf(buf, "(%s %s)", op, ast_to_string(ast->operand));
}

static void binop_to_string(String *buf, char *op, Ast *ast) {
  string_appendf(buf, "(%s %s %s)",
                 op, ast_to_string(ast->left), ast_to_string(ast->right));
}

static void ast_to_string_int(String *buf, Ast *ast) {
  if (!ast) {
    string_appendf(buf, "(nil)");
    return;
  }
  switch (ast->type) {
    case AST_LITERAL:
      switch (ast->ctype->type) {
        case CTYPE_INT:
          string_appendf(buf, "%d", ast->ival);
          break;
        case CTYPE_CHAR:
          string_appendf(buf, "'%c'", ast->c);
          break;
        default:
          error("internal error");
      }
      break;
    case AST_STRING:
      string_appendf(buf, "\"%s\"", quote_cstring(ast->sval));
      break;
    case AST_LVAR:
    case AST_GVAR:
      string_appendf(buf, "%s", ast->varname);
      break;
    case AST_FUNCALL: {
      string_appendf(buf, "(%s)%s(", ctype_to_string(ast->ctype), ast->fname);
      for (Iter *i = list_iter(ast->args); !iter_end(i);) {
        string_appendf(buf, "%s", ast_to_string(iter_next(i)));
        if (!iter_end(i))
          string_appendf(buf, ",");
      }
      string_appendf(buf, ")");
      break;
    }
    case AST_FUNC: {
      string_appendf(buf, "(%s)%s(", ctype_to_string(ast->ctype), ast->fname);
      for (Iter *i = list_iter(ast->params); !iter_end(i);) {
        Ast *param = iter_next(i);
        string_appendf(buf, "%s %s", ctype_to_string(param->ctype), ast_to_string(param));
        if (!iter_end(i))
          string_appendf(buf, ",");
      }
      string_appendf(buf, ")");
      ast_to_string_int(buf, ast->body);
      break;
    }
    case AST_DECL:
      string_appendf(buf, "(decl %s %s",
                     ctype_to_string(ast->declvar->ctype),
                     ast->declvar->varname);
      if (ast->declinit)
        string_appendf(buf, " %s)", ast_to_string(ast->declinit));
      else
        string_appendf(buf, ")");
      break;
    case AST_ARRAY_INIT:
      string_appendf(buf, "{");
      for (Iter *i = list_iter(ast->arrayinit); !iter_end(i);) {
        ast_to_string_int(buf, iter_next(i));
        if (!iter_end(i))
          string_appendf(buf, ",");
      }
      string_appendf(buf, "}");
      break;
    case AST_IF:
      string_appendf(buf, "(if %s %s",
                     ast_to_string(ast->cond),
                     ast_to_string(ast->then));
      if (ast->els)
        string_appendf(buf, " %s", ast_to_string(ast->els));
      string_appendf(buf, ")");
      break;
    case AST_TERNARY:
      string_appendf(buf, "(? %s %s %s)",
                     ast_to_string(ast->cond),
                     ast_to_string(ast->then),
                     ast_to_string(ast->els));
      break;
    case AST_FOR:
      string_appendf(buf, "(for %s %s %s ",
                     ast_to_string(ast->forinit),
                     ast_to_string(ast->forcond),
                     ast_to_string(ast->forstep));
      string_appendf(buf, "%s)", ast_to_string(ast->forbody));
      break;
    case AST_RETURN:
      string_appendf(buf, "(return %s)", ast_to_string(ast->retval));
      break;
    case AST_COMPOUND_STMT: {
      string_appendf(buf, "{");
      for (Iter *i = list_iter(ast->stmts); !iter_end(i);) {
        ast_to_string_int(buf, iter_next(i));
        string_appendf(buf, ";");
      }
      string_appendf(buf, "}");
      break;
    }
    case AST_ADDR:  uop_to_string(buf, "addr", ast); break;
    case AST_DEREF: uop_to_string(buf, "deref", ast); break;
    case PUNCT_INC: uop_to_string(buf, "++", ast); break;
    case PUNCT_DEC: uop_to_string(buf, "--", ast); break;
    case PUNCT_LOGAND: binop_to_string(buf, "and", ast); break;
    case PUNCT_LOGOR:  binop_to_string(buf, "or", ast); break;
    case '!': uop_to_string(buf, "!", ast); break;
    case '&': binop_to_string(buf, "&", ast); break;
    case '|': binop_to_string(buf, "|", ast); break;
    default: {
      char *left = ast_to_string(ast->left);
      char *right = ast_to_string(ast->right);
      if (ast->type == PUNCT_EQ)
        string_appendf(buf, "(== ");
      else
        string_appendf(buf, "(%c ", ast->type);
      string_appendf(buf, "%s %s)", left, right);
    }
  }
}

char *ast_to_string(Ast *ast) {
  String *s = make_string();
  ast_to_string_int(s, ast);
  return get_cstring(s);
}
