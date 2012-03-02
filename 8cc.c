#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#define BUFLEN 256

enum {
  AST_INT,
  AST_SYM,
};

typedef struct Var {
  char *name;
  int pos;
  struct Var *next;
} Var;

typedef struct Ast {
  char type;
  union {
    int ival;
    Var *var;
    struct {
      struct Ast *left;
      struct Ast *right;
    };
  };
} Ast;

Var *vars = NULL;

void error(char *fmt, ...) __attribute__((noreturn));
void emit_expr(Ast *ast);
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

Ast *make_ast_sym(Var *var) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_SYM;
  r->var = var;
  return r;
}

Var *find_var(char *name) {
  Var *v = vars;
  for (; v; v = v->next) {
    if (!strcmp(name, v->name))
      return v;
  }
  return NULL;
}

Var *make_var(char *name) {
  Var *v = malloc(sizeof(Var));
  v->name = name;
  v->pos = vars ? vars->pos + 1 : 1;
  v->next = vars;
  vars = v;
  return v;
}

void skip_space(void) {
  int c;
  while ((c = getc(stdin)) != EOF) {
    if (isspace(c))
      continue;
    ungetc(c, stdin);
    return;
  }
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

Ast *read_number(int n) {
  for (;;) {
    int c = getc(stdin);
    if (!isdigit(c)) {
      ungetc(c, stdin);
      return make_ast_int(n);
    }
    n = n * 10 + (c - '0');
  }
}

Ast *read_symbol(c) {
  char *buf = malloc(BUFLEN);
  buf[0] = c;
  int i = 1;
  for (;;) {
    int c = getc(stdin);
    if (!isalpha(c)) {
      ungetc(c, stdin);
      break;
    }
    buf[i++] = c;
    if (i == BUFLEN - 1)
      error("Symbol too long");
  }
  buf[i] = '\0';
  Var *v = find_var(buf);
  if (!v) v = make_var(buf);
  return make_ast_sym(v);
}

Ast *read_prim(void) {
  int c = getc(stdin);
  if (isdigit(c))
    return read_number(c - '0');
  if (isalpha(c))
    return read_symbol(c);
  else if (c == EOF)
    return NULL;
  error("Don't know how to handle '%c'", c);
}

Ast *read_expr2(int prec) {
  skip_space();
  Ast *ast = read_prim();
  if (!ast) return NULL;
  for (;;) {
    skip_space();
    int c = getc(stdin);
    if (c == EOF) return ast;
    int prec2 = priority(c);
    if (prec2 < 0 || prec2 < prec) {
      ungetc(c, stdin);
      return ast;
    }
    skip_space();
    ast = make_ast_op(c, ast, read_expr2(prec2 + 1));
  }
  return ast;
}

Ast *read_expr(void) {
  Ast *r = read_expr2(0);
  if (!r) return NULL;
  skip_space();
  int c = getc(stdin);
  if (c != ';')
    error("Unterminated expression");
  return r;
}

void emit_binop(Ast *ast) {
  if (ast->type == '=') {
    emit_expr(ast->right);
    if (ast->left->type != AST_SYM)
      error("Symbol expected");
    printf("mov %%eax, -%d(%%rbp)\n\t", ast->left->var->pos * 4);
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
    case AST_SYM:
      printf("mov -%d(%%rbp), %%eax\n\t", ast->var->pos * 4);
      break;
    default:
      emit_binop(ast);
  }
}

void print_ast(Ast *ast) {
  switch (ast->type) {
    case AST_INT:
      printf("%d", ast->ival);
      break;
    case AST_SYM:
      printf("%s", ast->var->name);
      break;
    default:
      printf("(%c ", ast->type);
      print_ast(ast->left);
      printf(" ");
      print_ast(ast->right);
      printf(")");
  }
}

int main(int argc, char **argv) {
  int wantast = (argc > 1 && !strcmp(argv[1], "-a"));
  if (!wantast) {
    printf(".text\n\t"
           ".global mymain\n"
           "mymain:\n\t");
  }
  for (;;) {
    Ast *ast = read_expr();
    if (!ast) break;
    if (wantast)
      print_ast(ast);
    else
      emit_expr(ast);
  }
  if (!wantast)
    printf("ret\n");
  return 0;
}
