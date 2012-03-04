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
};

typedef struct Ast {
  char type;
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
  };
} Ast;

Ast *vars = NULL;
Ast *strings = NULL;
char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

void emit_expr(Ast *ast);
Ast *read_expr2(int prec);
Ast *read_expr(void);

void error(char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
  exit(1);
}

Ast *make_ast_op(char type, Ast *left, Ast *right) {
  Ast *r = malloc(sizeof(Ast));
  r->type = type;
  r->left = left;
  r->right = right;
  return r;
}

Ast *make_ast_int(int val) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_INT;
  r->ival = val;
  return r;
}

Ast *make_ast_char(char c) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_CHAR;
  r->c = c;
  return r;
}

Ast *make_ast_var(char *vname) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_VAR;
  r->vname = vname;
  r->vpos = vars ? vars->vpos + 1 : 1;
  r->vnext = vars;
  vars = r;
  return r;
}

Ast *make_ast_string(char *str) {
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

Ast *make_ast_funcall(char *fname, int nargs, Ast **args) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_FUNCALL;
  r->fname = fname;
  r->nargs = nargs;
  r->args = args;
  return r;
}

Ast *find_var(char *name) {
  for (Ast *p = vars; p; p = p->vnext) {
    if (!strcmp(name, p->vname))
      return p;
  }
  return NULL;
}

int priority(char op) {
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

Ast *read_func_args(char *fname) {
  Ast **args = malloc(sizeof(Ast*) * (MAX_ARGS + 1));
  int i = 0, nargs = 0;
  for (; i < MAX_ARGS; i++) {
    Token *tok = read_token();
    if (is_punct(tok, ')')) break;
    unget_token(tok);
    args[i] = read_expr2(0);
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

Ast *read_ident_or_func(char *name) {
  Token *tok = read_token();
  if (is_punct(tok, '('))
    return read_func_args(name);
  unget_token(tok);
  Ast *v = find_var(name);
  return v ? v : make_ast_var(name);
}

Ast *read_prim(void) {
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

Ast *read_expr2(int prec) {
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
    ast = make_ast_op(tok->punct, ast, read_expr2(prec2 + 1));
  }
}

Ast *read_expr(void) {
  Ast *r = read_expr2(0);
  if (!r) return NULL;
  Token *tok = read_token();
  if (!is_punct(tok, ';'))
    error("Unterminated expression: %s", token_to_string(tok));
  return r;
}

void emit_binop(Ast *ast) {
  if (ast->type == '=') {
    emit_expr(ast->right);
    if (ast->left->type != AST_VAR)
      error("Symbol expected");
    printf("mov %%eax, -%d(%%rbp)\n\t", ast->left->vpos * 4);
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

void emit_expr(Ast *ast) {
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
    default:
      emit_binop(ast);
  }
}

void print_quote(char *p) {
  while (*p) {
    if (*p == '\"' || *p == '\\')
      printf("\\");
    printf("%c", *p);
    p++;
  }
}

void print_ast(Ast *ast) {
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
    default:
      printf("(%c ", ast->type);
      print_ast(ast->left);
      printf(" ");
      print_ast(ast->right);
      printf(")");
  }
}

void emit_data_section(void) {
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
    Ast *t = read_expr();
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
