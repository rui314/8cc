#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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

void error(char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
  exit(1);
}

static Ast *make_ast_op(char type, Ast *left, Ast *right) {
  Ast *r = malloc(sizeof(Ast));
  r->type = type;
  r->left = left;
  r->right = right;
  return r;
}

static Ast *make_ast_int(int val) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_INT;
  r->ival = val;
  return r;
}

static Ast *make_ast_char(char c) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_CHAR;
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
    ast = make_ast_op(tok->punct, ast, read_expr(prec2 + 1));
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

static void print_quote(char *p) {
  while (*p) {
    if (*p == '\"' || *p == '\\')
      printf("\\");
    printf("%c", *p);
    p++;
  }
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

static void print_ast(Ast *ast) {
  switch (ast->type) {
    case AST_INT:
      printf("%d", ast->ival);
      break;
    case AST_CHAR:
      printf("'%c'", ast->c);
      break;
    case AST_VAR:
      printf("%s", ast->vname);
      break;
    case AST_STR:
      printf("\"");
      print_quote(ast->sval);
      printf("\"");
      break;
    case AST_FUNCALL:
      printf("%s(", ast->fname);
      for (int i = 0; ast->args[i]; i++) {
        print_ast(ast->args[i]);
        if (ast->args[i + 1])
          printf(",");
      }
      printf(")");
      break;
    case AST_DECL:
      printf("(decl %s %s ",
             ctype_to_string(ast->decl_var->ctype),
             ast->decl_var->vname);
      print_ast(ast->decl_init);
      printf(")");
      break;
    default:
      printf("(%c ", ast->type);
      print_ast(ast->left);
      printf(" ");
      print_ast(ast->right);
      printf(")");
  }
}

static void emit_data_section(void) {
  if (!strings) return;
  printf("\t.data\n");
  for (Ast *p = strings; p; p = p->snext) {
    printf(".s%d:\n\t", p->sid);
    printf(".string \"");
    print_quote(p->sval);
    printf("\"\n");
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
      print_ast(exprs[i]);
    else
      emit_expr(exprs[i]);
  }
  if (!wantast)
    printf("ret\n");
  return 0;
}
