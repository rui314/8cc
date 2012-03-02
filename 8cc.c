#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#define BUFLEN 256

enum {
  AST_INT,
  AST_STR,
};

typedef struct Ast {
  char type;
  union {
    int ival;
    char *sval;
    struct {
      struct Ast *left;
      struct Ast *right;
    };
  };
}  Ast;

void error(char *fmt, ...) __attribute__((noreturn));
void emit_intexpr(Ast *ast);
Ast *read_string(void);
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

Ast *make_ast_str(char *str) {
  Ast *r = malloc(sizeof(Ast));
  r->type = AST_STR;
  r->sval = str;
  return r;
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
    case '+':
    case '-':
        return 1;
    case '*':
    case '/':
        return 2;
    default:
      error("Unknown binary operator: %c", op);
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

Ast *read_prim(void) {
  int c = getc(stdin);
  if (isdigit(c))
    return read_number(c - '0');
  else if (c == '"')
    return read_string();
  else if (c == EOF)
    error("Unexpected EOF");
  error("Don't know how to handle '%c'", c);
}

Ast *read_expr2(int prec) {
  Ast *ast = read_prim();
  for (;;) {
    skip_space();
    int c = getc(stdin);
    if (c == EOF) return ast;
    int prec2 = priority(c);
    if (prec2 < prec) {
      ungetc(c, stdin);
      return ast;
    }
    skip_space();
    ast = make_ast_op(c, ast, read_expr2(prec2 + 1));
  }
  return ast;
}

Ast *read_string(void) {
  char *buf = malloc(BUFLEN);
  int i = 0;
  for (;;) {
    int c = getc(stdin);
    if (c == EOF)
      error("Unterminated string");
    if (c == '"')
      break;
    if (c == '\\') {
      c = getc(stdin);
      if (c == EOF) error("Unterminated \\");
    }
    buf[i++] = c;
    if (i == BUFLEN - 1)
      error("String too long");
  }
  buf[i] = '\0';
  return make_ast_str(buf);
}

Ast *read_expr(void) {
  return read_expr2(0);
}

void print_quote(char *p) {
  while (*p) {
    if (*p == '\"' || *p == '\\')
      printf("\\");
    printf("%c", *p);
    p++;
  }
}

void emit_string(Ast *ast) {
  printf("\t.data\n"
         ".mydata:\n\t"
         ".string \"");
  print_quote(ast->sval);
  printf("\"\n\t"
         ".text\n\t"
         ".global stringfn\n"
         "stringfn:\n\t"
         "lea .mydata(%%rip), %%rax\n\t"
         "ret\n");
  return;
}

void emit_binop(Ast *ast) {
  char *op;
  switch (ast->type) {
    case '+': op = "add"; break;
    case '-': op = "sub"; break;
    case '*': op = "imul"; break;
    case '/': break;
    default: error("invalid operator '%c'", ast->type);
  }
  emit_intexpr(ast->left);
  printf("push %%rax\n\t");
  emit_intexpr(ast->right);
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

void ensure_intexpr(Ast *ast) {
  switch (ast->type) {
    case '+': case '-': case '*': case '/': case AST_INT:
      return;
    default:
      error("integer or binary operator expected");
  }
}

void emit_intexpr(Ast *ast) {
  ensure_intexpr(ast);
  if (ast->type == AST_INT)
    printf("mov $%d, %%eax\n\t", ast->ival);
  else
    emit_binop(ast);
}

void print_ast(Ast *ast) {
  switch (ast->type) {
    case AST_INT:
      printf("%d", ast->ival);
      break;
    case AST_STR:
      print_quote(ast->sval);
      break;
    default:
      printf("(%c ", ast->type);
      print_ast(ast->left);
      printf(" ");
      print_ast(ast->right);
      printf(")");
  }
}

void compile(Ast *ast) {
  if (ast->type == AST_STR) {
    emit_string(ast);
  } else {
    printf(".text\n\t"
           ".global intfn\n"
           "intfn:\n\t");
    emit_intexpr(ast);
    printf("ret\n");
  }
}

int main(int argc, char **argv) {
  Ast *ast = read_expr();
  if (argc > 1 && !strcmp(argv[1], "-a"))
    print_ast(ast);
  else
    compile(ast);
  return 0;
}
