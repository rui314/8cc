#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

#define EXPR_LEN 100
#define MAX_ARGS 6

enum {
  AST_INT,
  AST_CHAR,
  AST_VAR,
  AST_STR,
  AST_FUNCALL,
  AST_DECL,
};

enum {
  CTYPE_VOID,
  CTYPE_INT,
  CTYPE_CHAR,
  CTYPE_STR,
};

typedef struct Ast {
  char type;
  char ctype;
  union {
    // Integer
    int ival;
    // Char
    char c;
    // String
    struct {
      char *sval;
      int sid;
      struct Ast *snext;
    };
    // Variable
    struct {
      char *vname;
      int vpos;
      struct Ast *vnext;
    };
    // Binary operator
    struct {
      struct Ast *left;
      struct Ast *right;
    };
    // Function call
    struct {
      char *fname;
      int nargs;
      struct Ast **args;
    };
    // Declaration
    struct {
      struct Ast *decl_var;
      struct Ast *decl_init;
    };
  };
} Ast;

static Ast *vars = NULL;
static Ast *strings = NULL;
static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

static void emit_expr(Ast *ast);
static Ast *read_expr(int prec);
static char *ast_to_string(Ast *ast);

static Ast *make_ast_op(char type, char ctype, Ast *left, Ast *right) {
  Ast *r = malloc(sizeof(Ast));
  r->type = type;
  r->ctype = ctype;
  r->left = left;
  r->right = right;
  return r;
}

static Ast *make_ast_int(int val) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_INT;
  r->ctype = CTYPE_INT;
  r->ival = val;
  return r;
}

static Ast *make_ast_char(char c) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_CHAR;
  r->ctype = CTYPE_CHAR;
  r->c = c;
  return r;
}

static Ast *make_ast_var(int ctype, char *vname) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_VAR;
  r->ctype = ctype;
  r->vname = vname;
  r->vpos = vars ? vars->vpos + 1 : 1;
  r->vnext = vars;
  vars = r;
  return r;
}

static Ast *make_ast_string(char *str) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_STR;
  r->ctype = CTYPE_STR;
  r->sval = str;
  if (strings == NULL) {
    r->sid = 0;
    r->snext = NULL;
  } else {
    r->sid = strings->sid + 1;
    r->snext = strings;
  }
  strings = r;
  return r;
}

static Ast *make_ast_funcall(char *fname, int nargs, Ast **args) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_FUNCALL;
  r->ctype = CTYPE_INT;
  r->fname = fname;
  r->nargs = nargs;
  r->args = args;
  return r;
}

static Ast *make_ast_decl(Ast *var, Ast *init) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_DECL;
  r->decl_var = var;
  r->decl_init = init;
  return r;
}

static Ast *find_var(char *name) {
  for (Ast *p = vars; p; p = p->vnext) {
    if (!strcmp(name, p->vname))
      return p;
  }
  return NULL;
}

static bool is_right_assoc(char op) {
  return op == '=';
}

static int priority(char op) {
  switch (op) {
    case '=':
      return 1;
    case '+': case '-':
      return 2;
    case '*': case '/':
      return 3;
    default:
      return -1;
  }
}

static Ast *read_func_args(char *fname) {
  Ast **args = malloc(sizeof(Ast*) * (MAX_ARGS + 1));
  int i = 0, nargs = 0;
  for (; i < MAX_ARGS; i++) {
    Token *tok = read_token();
    if (is_punct(tok, ')')) break;
    unget_token(tok);
    args[i] = read_expr(0);
    nargs++;
    tok = read_token();
    if (is_punct(tok, ')')) break;
    if (!is_punct(tok, ','))
      error("Unexpected token: '%s'", token_to_string(tok));
  }
  if (i == MAX_ARGS)
    error("Too many arguments: %s", fname);
  return make_ast_funcall(fname, nargs, args);
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
      return make_ast_int(tok->ival);
    case TTYPE_CHAR:
      return make_ast_char(tok->c);
    case TTYPE_STRING:
      return make_ast_string(tok->sval);
    case TTYPE_PUNCT:
      error("unexpected character: '%c'", tok->punct);
    default:
      error("internal error: unknown token type: %d", tok->type);
  }
}

static void ensure_lvalue(Ast *ast) {
  if (ast->type != AST_VAR)
    error("variable expected");
}

#define swap(a, b)                              \
  { typeof(a) tmp = b; b = a; a = tmp; }

static char result_type(char op, Ast *a, Ast *b) {
  int swapped = false;
  if (a->ctype > b->ctype) {
    swapped = true;
    swap(a, b);
  }
  switch (a->ctype) {
    case CTYPE_VOID:
      goto err;
    case CTYPE_INT:
      switch (b->ctype) {
        case CTYPE_INT:
        case CTYPE_CHAR:
          return CTYPE_INT;
        case CTYPE_STR:
          goto err;
      }
      error("internal error");
    case CTYPE_CHAR:
      switch (b->ctype) {
        case CTYPE_CHAR:
          return CTYPE_INT;
        case CTYPE_STR:
          goto err;
      }
      error("internal error");
    case CTYPE_STR:
      goto err;
    default:
      error("internal error");
  }
err:
  if (swapped) swap(a, b);
  error("incompatible operands: %s and %s for %c",
        ast_to_string(a), ast_to_string(b), op);
}

static Ast *read_expr(int prec) {
  Ast *ast = read_prim();
  if (!ast) return NULL;
  for (;;) {
    Token *tok = read_token();
    if (tok->type != TTYPE_PUNCT) {
      unget_token(tok);
      return ast;
    }
    int prec2 = priority(tok->punct);
    if (prec2 < 0 || prec2 < prec) {
      unget_token(tok);
      return ast;
    }
    if (is_punct(tok, '='))
      ensure_lvalue(ast);
    Ast *rest = read_expr(prec2 + (is_right_assoc(tok->punct) ? 0 : 1));
    char ctype = result_type(tok->punct, ast, rest);
    ast = make_ast_op(tok->punct, ctype, ast, rest);
  }
}

static int get_ctype(Token *tok) {
  if (tok->type != TTYPE_IDENT)
    return -1;
  if (!strcmp(tok->sval, "int"))
    return CTYPE_INT;
  if (!strcmp(tok->sval, "char"))
    return CTYPE_CHAR;
  if (!strcmp(tok->sval, "string"))
    return CTYPE_STR;
  return -1;
}


static bool is_type_keyword(Token *tok) {
  return get_ctype(tok) != -1;
}

static void expect(char punct) {
  Token *tok = read_token();
  if (!is_punct(tok, punct))
    error("'%c' expected, but got %s", punct, token_to_string(tok));
}

static Ast *read_decl(void) {
  int ctype = get_ctype(read_token());
  Token *name = read_token();
  if (name->type != TTYPE_IDENT)
    error("Identifier expected, but got %s", token_to_string(name));
  Ast *var = make_ast_var(ctype, name->sval);
  expect('=');
  Ast *init = read_expr(0);
  return make_ast_decl(var, init);
}

static Ast *read_decl_or_stmt(void) {
  Token *tok = peek_token();
  if (!tok) return NULL;
  Ast *r = is_type_keyword(tok) ? read_decl() : read_expr(0);
  tok = read_token();
  if (!is_punct(tok, ';'))
    error("Unterminated expression: %s", token_to_string(tok));
  return r;
}

static void emit_assign(Ast *var, Ast *value) {
  emit_expr(value);
  printf("mov %%eax, -%d(%%rbp)\n\t", var->vpos * 4);
}

static void emit_binop(Ast *ast) {
  if (ast->type == '=') {
    emit_assign(ast->left, ast->right);
    return;
  }
  char *op;
  switch (ast->type) {
    case '+': op = "add"; break;
    case '-': op = "sub"; break;
    case '*': op = "imul"; break;
    case '/': break;
    default: error("invalid operator '%c'", ast->type);
  }
  emit_expr(ast->left);
  printf("push %%rax\n\t");
  emit_expr(ast->right);
  if (ast->type == '/') {
    printf("mov %%eax, %%ebx\n\t");
    printf("pop %%rax\n\t");
    printf("mov $0, %%edx\n\t");
    printf("idiv %%ebx\n\t");
  } else {
    printf("pop %%rbx\n\t");
    printf("%s %%ebx, %%eax\n\t", op);
  }
}

static void emit_expr(Ast *ast) {
  switch (ast->type) {
    case AST_INT:
      printf("mov $%d, %%eax\n\t", ast->ival);
      break;
    case AST_CHAR:
      printf("mov $%d, %%eax\n\t", ast->c);
      break;
    case AST_VAR:
      printf("mov -%d(%%rbp), %%eax\n\t", ast->vpos * 4);
      break;
    case AST_STR:
      printf("lea .s%d(%%rip), %%rax\n\t", ast->sid);
      break;
    case AST_FUNCALL:
      for (int i = 1; i < ast->nargs; i++)
        printf("push %%%s\n\t", REGS[i]);
      for (int i = 0; i < ast->nargs; i++) {
        emit_expr(ast->args[i]);
        printf("push %%rax\n\t");
      }
      for (int i = ast->nargs - 1; i >= 0; i--)
        printf("pop %%%s\n\t", REGS[i]);
      printf("mov $0, %%eax\n\t");
      printf("call %s\n\t", ast->fname);
      for (int i = ast->nargs - 1; i > 0; i--)
        printf("pop %%%s\n\t", REGS[i]);
      break;
    case AST_DECL:
      emit_assign(ast->decl_var, ast->decl_init);
      return;
    default:
      emit_binop(ast);
  }
}

static char *quote(char *p) {
  String *s = make_string();
  while (*p) {
    if (*p == '\"' || *p == '\\')
      string_append(s, '\\');
    string_append(s, *p);
    p++;
  }
  return get_cstring(s);
}

static char *ctype_to_string(int ctype) {
  switch (ctype) {
    case CTYPE_VOID: return "void";
    case CTYPE_INT:  return "int";
    case CTYPE_CHAR: return "char";
    case CTYPE_STR:  return "string";
    default: error("Unknown ctype: %d", ctype);
  }
}

static void ast_to_string_int(Ast *ast, String *buf) {
  switch (ast->type) {
    case AST_INT:
      string_appendf(buf, "%d", ast->ival);
      break;
    case AST_CHAR:
      string_appendf(buf, "'%c'", ast->c);
      break;
    case AST_VAR:
      string_appendf(buf, "%s", ast->vname);
      break;
    case AST_STR:
      string_appendf(buf, "\"%s\"", quote(ast->sval));
      break;
    case AST_FUNCALL:
      string_appendf(buf, "%s(", ast->fname);
      for (int i = 0; ast->args[i]; i++) {
        string_appendf(buf, "%s", ast_to_string(ast->args[i]));
        if (ast->args[i + 1])
          string_appendf(buf, ",");
      }
      string_appendf(buf, ")");
      break;
    case AST_DECL:
      string_appendf(buf, "(decl %s %s %s)",
                     ctype_to_string(ast->decl_var->ctype),
                     ast->decl_var->vname,
                     ast_to_string(ast->decl_init));
      break;
    default: {
      char *left = ast_to_string(ast->left);
      char *right = ast_to_string(ast->right);
      string_appendf(buf, "(%c %s %s)", ast->type, left, right);
    }
  }
}

static char *ast_to_string(Ast *ast) {
  String *s = make_string();
  ast_to_string_int(ast, s);
  return get_cstring(s);
}

static void emit_data_section(void) {
  if (!strings) return;
  printf("\t.data\n");
  for (Ast *p = strings; p; p = p->snext) {
    printf(".s%d:\n\t", p->sid);
    printf(".string \"%s\"\n", quote(p->sval));
  }
  printf("\t");
}

int main(int argc, char **argv) {
  int wantast = (argc > 1 && !strcmp(argv[1], "-a"));
  Ast *exprs[EXPR_LEN];
  int i;
  for (i = 0; i < EXPR_LEN; i++) {
    Ast *t = read_decl_or_stmt();
    if (!t) break;
    exprs[i] = t;
  }
  int nexpr = i;
  if (!wantast) {
    emit_data_section();
    printf(".text\n\t"
           ".global mymain\n"
           "mymain:\n\t");
  }
  for (i = 0; i < nexpr; i++) {
    if (wantast)
      printf("%s", ast_to_string(exprs[i]));
    else
      emit_expr(exprs[i]);
  }
  if (!wantast)
    printf("ret\n");
  return 0;
}
