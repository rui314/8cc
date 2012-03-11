#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "8cc.h"

#define MAX_ARGS 6

List *globals = EMPTY_LIST;
List *fparams = NULL;
List *locals = NULL;
Ctype *ctype_int = &(Ctype){ CTYPE_INT, NULL };
Ctype *ctype_char = &(Ctype){ CTYPE_CHAR, NULL };

static int labelseq = 0;

static Ast *read_expr(int prec);
static Ctype* make_ptr_type(Ctype *ctype);
static Ctype* make_array_type(Ctype *ctype, int size);
static void ast_to_string_int(Ast *ast, String *buf);
static List *read_block(void);
static Ast *read_decl_or_stmt(void);
static Ctype *result_type(char op, Ctype *a, Ctype *b);
static Ctype *convert_array(Ctype *ctype);

static Ast *ast_uop(char type, Ctype *ctype, Ast *operand) {
  Ast *r = malloc(sizeof(Ast));
  r->type = type;
  r->ctype = ctype;
  r->operand = operand;
  return r;
}

static Ast *ast_binop(char type, Ast *left, Ast *right) {
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
  r->lname = name;
  if (locals)
    list_append(locals, r);
  return r;
}

static Ast *ast_gvar(Ctype *ctype, char *name, bool filelocal) __attribute__((unused));
static Ast *ast_gvar(Ctype *ctype, char *name, bool filelocal) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_GVAR;
  r->ctype = ctype;
  r->gname = name;
  r->glabel = filelocal ? make_label() : name;
  list_append(globals, r);
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

static Ast *ast_func(Ctype *rettype, char *fname, List *params, List *body, List *locals) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_FUNC;
  r->ctype = rettype;
  r->fname = fname;
  r->params = params;
  r->locals = locals;
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

static Ast *ast_if(Ast *cond, List *then, List *els) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_IF;
  r->ctype = NULL;
  r->cond = cond;
  r->then = then;
  r->els = els;
  return r;
}

static Ast *ast_for(Ast *init, Ast *cond, Ast *step, List *body) {
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

static Ast *find_var_sub(List *list, char *name) {
  for (Iter *i = list_iter(list); !iter_end(i);) {
    Ast *v = iter_next(i);
    if (!strcmp(name, v->lname))
      return v;
  }
  return NULL;
}

static Ast *find_var(char *name) {
  Ast *r = find_var_sub(locals, name);
  if (r) return r;
  r = find_var_sub(fparams, name);
  if (r) return r;
  return find_var_sub(globals, name);
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
    case '=':
      return 1;
    case '@':
      return 2;
    case '<': case '>':
      return 3;
    case '+': case '-':
      return 4;
    case '*': case '/':
      return 5;
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
    list_append(args, read_expr(0));
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
      list_append(globals, r);
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
      goto err;
    default:
      error("internal error");
  }
err:
  longjmp(*jmpbuf, 1);
}

static Ast *read_subscript_expr(Ast *ast) {
  Ast *sub = read_expr(0);
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

static void ensure_lvalue(Ast *ast) {
  switch (ast->type) {
    case AST_LVAR: case AST_GVAR: case AST_DEREF:
      return;
    default:
      error("lvalue expected, but got %s", ast_to_string(ast));
  }
}

static Ast *read_unary_expr(void) {
  Token *tok = read_token();
  if (tok->type != TTYPE_PUNCT) {
    unget_token(tok);
    return read_postfix_expr();
  }
  if (is_punct(tok, '(')) {
    Ast *r = read_expr(0);
    expect(')');
    return r;
  }
  if (is_punct(tok, '&')) {
    Ast *operand = read_unary_expr();
    ensure_lvalue(operand);
    return ast_uop(AST_ADDR, make_ptr_type(operand->ctype), operand);
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

static Ast *read_expr(int prec) {
  Ast *ast = read_unary_expr();
  if (!ast) return NULL;
  for (;;) {
    Token *tok = read_token();
    if (tok->type != TTYPE_PUNCT) {
      unget_token(tok);
      return ast;
    }
    int prec2 = priority(tok);
    if (prec2 < 0 || prec2 < prec) {
      unget_token(tok);
      return ast;
    }
    if (is_punct(tok, '='))
      ensure_lvalue(ast);
    Ast *rest = read_expr(prec2 + (is_right_assoc(tok) ? 0 : 1));
    ast = ast_binop(tok->punct, ast, rest);
  }
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
    Ast *init = read_expr(0);
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

static Ast *read_decl_array_init(Ast *var) {
  Ast *init;
  if (var->ctype->type == CTYPE_ARRAY) {
    init = read_decl_array_init_int(var->ctype);
    int len = (init->type == AST_STRING)
        ? strlen(init->sval) + 1
        : list_len(init->arrayinit);
    if (var->ctype->size == -1) {
      var->ctype->size = len;
    } else if (var->ctype->size != len)
      error("Invalid array initializer: expected %d items but got %d",
            var->ctype->size, len);
  } else {
    init = read_expr(0);
  }
  expect(';');
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
    Ast *size = read_expr(0);
    if (size->type != AST_LITERAL || size->ctype->type != CTYPE_INT)
      error("Integer expected, but got %s", ast_to_string(size));
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

static Ast *read_decl(void) {
  Ctype *ctype = read_decl_spec();
  Token *varname = read_token();
  if (varname->type != TTYPE_IDENT)
    error("Identifier expected, but got %s", token_to_string(varname));
  ctype = read_array_dimensions(ctype);
  Ast *var = ast_lvar(ctype, varname->sval);
  Token *tok = read_token();
  if (is_punct(tok, '='))
    return read_decl_array_init(var);
  unget_token(tok);
  expect(';');
  return ast_decl(var, NULL);
}

static Ast *read_if_stmt(void) {
  expect('(');
  Ast *cond = read_expr(0);
  expect(')');
  expect('{');
  List *then = read_block();
  Token *tok = read_token();
  if (!tok || tok->type != TTYPE_IDENT || strcmp(tok->sval, "else")) {
    unget_token(tok);
    return ast_if(cond, then, NULL);
  }
  expect('{');
  List *els = read_block();
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
  Ast *r = read_expr(0);
  expect(';');
  return r;
}

static Ast *read_for_stmt(void) {
  expect('(');
  Ast *init = read_opt_decl_or_stmt();
  Ast *cond = read_opt_expr();
  Ast *step = is_punct(peek_token(), ')')
      ? NULL : read_expr(0);
  expect(')');
  expect('{');
  List *body = read_block();
  return ast_for(init, cond, step, body);
}

static Ast *read_return_stmt(void) {
  Ast *retval = read_expr(0);
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
  unget_token(tok);
  Ast *r = read_expr(0);
  expect(';');
  return r;
}

static Ast *read_decl_or_stmt(void) {
  Token *tok = peek_token();
  if (!tok) return NULL;
  return is_type_keyword(tok) ? read_decl() : read_stmt();
}

static List *read_block(void) {
  List *r = make_list();
  for (;;) {
    Ast *stmt = read_decl_or_stmt();
    if (stmt) list_append(r, stmt);
    if (!stmt) break;
    Token *tok = read_token();
    if (is_punct(tok, '}'))
      break;
    unget_token(tok);
  }
  return r;
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

static Ast *read_func_decl(void) {
  Token *tok = peek_token();
  if (!tok) return NULL;
  void *rettype = read_decl_spec();
  Token *fname = read_token();
  if (fname->type != TTYPE_IDENT)
    error("Function name expected, but got %s", token_to_string(fname));
  expect('(');
  fparams = read_params();
  expect('{');
  locals = make_list();
  List *body = read_block();
  Ast *r = ast_func(rettype, fname->sval, fparams, body, locals);
  fparams = locals = NULL;
  return r;
}

List *read_func_list(void) {
  List *r = make_list();
  for (;;) {
    Ast *func = read_func_decl();
    if (!func) return r;
    list_append(r, func);
  }
}

char *ctype_to_string(Ctype *ctype) {
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

static char *block_to_string(List *block) {
  String *s = make_string();
  string_appendf(s, "{");
  for (Iter *i = list_iter(block); !iter_end(i);) {
    ast_to_string_int(iter_next(i), s);
    string_appendf(s, ";");
  }
  string_appendf(s, "}");
  return get_cstring(s);
}

static void ast_to_string_int(Ast *ast, String *buf) {
  if (!ast) {
    string_appendf(buf, "(null)");
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
      string_appendf(buf, "%s", ast->lname);
      break;
    case AST_GVAR:
      string_appendf(buf, "%s", ast->gname);
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
      string_appendf(buf, ")%s", block_to_string(ast->body));
      break;
    }
    case AST_DECL:
      string_appendf(buf, "(decl %s %s",
                     ctype_to_string(ast->declvar->ctype),
                     ast->declvar->lname);
      if (ast->declinit)
        string_appendf(buf, " %s)", ast_to_string(ast->declinit));
      else
        string_appendf(buf, ")");
      break;
    case AST_ARRAY_INIT:
      string_appendf(buf, "{");
      for (Iter *i = list_iter(ast->arrayinit); !iter_end(i);) {
        ast_to_string_int(iter_next(i), buf);
        if (!iter_end(i))
          string_appendf(buf, ",");
      }
      string_appendf(buf, "}");
      break;
    case AST_ADDR:
      string_appendf(buf, "(& %s)", ast_to_string(ast->operand));
      break;
    case AST_DEREF:
      string_appendf(buf, "(* %s)", ast_to_string(ast->operand));
      break;
    case AST_IF:
      string_appendf(buf, "(if %s %s",
                     ast_to_string(ast->cond),
                     block_to_string(ast->then));
      if (ast->els)
        string_appendf(buf, " %s", block_to_string(ast->els));
      string_appendf(buf, ")");
      break;
    case AST_FOR:
      string_appendf(buf, "(for %s %s %s ",
                     ast_to_string(ast->forinit),
                     ast_to_string(ast->forcond),
                     ast_to_string(ast->forstep));
      string_appendf(buf, "%s)", block_to_string(ast->forbody));
      break;
    case AST_RETURN:
      string_appendf(buf, "(return %s)", ast_to_string(ast->retval));
      break;
    default: {
      char *left = ast_to_string(ast->left);
      char *right = ast_to_string(ast->right);
      if (ast->type == '@')
        string_appendf(buf, "(== ");
      else
        string_appendf(buf, "(%c ", ast->type);
      string_appendf(buf, "%s %s)", left, right);
    }
  }
}

char *ast_to_string(Ast *ast) {
  String *s = make_string();
  ast_to_string_int(ast, s);
  return get_cstring(s);
}
