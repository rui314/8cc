#include <stdio.h>
#include "8cc.h"

static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

static void emit_expr(Ast *ast);
static void emit_block(List *block);

static int ctype_size(Ctype *ctype) {
  switch (ctype->type) {
    case CTYPE_CHAR: return 1;
    case CTYPE_INT:  return 4;
    case CTYPE_PTR:  return 8;
    case CTYPE_ARRAY:
      return ctype_size(ctype->ptr) * ctype->size;
    default:
      error("internal error");
  }
}

static void emit_gload(Ctype *ctype, char *label, int off) {
  if (ctype->type == CTYPE_ARRAY) {
    printf("lea %s(%%rip), %%rax\n\t", label);
    if (off)
      printf("add $%d, %%rax\n\t", ctype_size(ctype->ptr) * off);
    return;
  }
  char *reg;
  int size = ctype_size(ctype);
  switch (size) {
    case 1: reg = "al"; printf("mov $0, %%eax\n\t"); break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
    default:
      error("Unknown data size: %s: %d", ctype_to_string(ctype), size);
  }
  printf("mov %s(%%rip), %%%s\n\t", label, reg);
  if (off)
    printf("add $%d, %%rax\n\t", off * size);
  printf("mov (%%rax), %%%s\n\t", reg);
}

static void emit_lload(Ast *var, int off) {
  if (var->ctype->type == CTYPE_ARRAY) {
    printf("lea %d(%%rbp), %%rax\n\t", -var->loff);
    return;
  }
  int size = ctype_size(var->ctype);
  switch (size) {
    case 1:
      printf("mov $0, %%eax\n\t");
      printf("mov %d(%%rbp), %%al\n\t", -var->loff);
      break;
    case 4:
      printf("mov %d(%%rbp), %%eax\n\t", -var->loff);
      break;
    case 8:
      printf("mov %d(%%rbp), %%rax\n\t", -var->loff);
      break;
    default:
      error("Unknown data size: %s: %d", ast_to_string(var), size);
  }
  if (off)
    printf("add $%d, %%rax\n\t", var->loff * size);
}

static void emit_gsave(Ast *var, int off) {
  assert(var->ctype->type != CTYPE_ARRAY);
  char *reg;
  printf("push %%rcx\n\t");
  printf("mov %s(%%rip), %%rcx\n\t", var->glabel);
  int size = ctype_size(var->ctype);
  switch (size) {
    case 1: reg = "al";  break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
    default:
      error("Unknown data size: %s: %d", ast_to_string(var), size);
  }
  printf("mov %s, %d(%%rbp)\n\t", reg, off * size);
  printf("pop %%rcx\n\t");
}

static void emit_lsave(Ctype *ctype, int loff, int off) {
  char *reg;
  int size = ctype_size(ctype);
  switch (size) {
    case 1: reg = "al";  break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
  }
  printf("mov %%%s, %d(%%rbp)\n\t", reg, -(loff + off * size));
}

static void emit_deref(Ast *var, Ast *value) {
  emit_expr(var->operand);
  printf("push %%rax\n\t");
  emit_expr(value);
  printf("pop %%rcx\n\t");
  char *reg;
  int size = ctype_size(var->operand->ctype);
  switch (size) {
    case 1: reg = "al";  break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
  }
  printf("mov %%%s, (%%rcx)\n\t", reg);
}

static void emit_pointer_arith(char op, Ast *left, Ast *right) {
  assert(left->ctype->type == CTYPE_PTR);
  emit_expr(left);
  printf("push %%rax\n\t");
  emit_expr(right);
  int size = ctype_size(left->ctype->ptr);
  if (size > 1)
    printf("imul $%d, %%rax\n\t", size);
  printf("mov %%rax, %%rcx\n\t"
         "pop %%rax\n\t"
         "add %%rcx, %%rax\n\t");
}

static void emit_assign(Ast *var, Ast *value) {
  emit_expr(value);
  switch (var->type) {
    case AST_LVAR: emit_lsave(var->ctype, var->loff, 0); break;
    case AST_LREF: emit_lsave(var->lref->ctype, var->lref->loff, var->loff); break;
    case AST_GVAR: emit_gsave(var, 0); break;
    case AST_GREF: emit_gsave(var->gref, var->goff); break;
    case AST_DEREF: emit_deref(var, value); break;
    default: error("internal error");
  }
}

static void emit_comp(Ast *a, Ast *b) {
  emit_expr(a);
  printf("push %%rax\n\t");
  emit_expr(b);
  printf("pop %%rcx\n\t");
  printf("cmp %%rax, %%rcx\n\t");
  printf("setl %%al\n\t");
  printf("movzb %%al, %%eax\n\t");
}

static void emit_binop(Ast *ast) {
  if (ast->type == '=') {
    emit_assign(ast->left, ast->right);
    return;
  }
  if (ast->ctype->type == CTYPE_PTR) {
    emit_pointer_arith(ast->type, ast->left, ast->right);
    return;
  }
  char *op;
  switch (ast->type) {
    case '<':
      emit_comp(ast->left, ast->right);
      return;
    case '>':
      emit_comp(ast->right, ast->left);
      return;
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
    printf("mov %%rax, %%rcx\n\t");
    printf("pop %%rax\n\t");
    printf("mov $0, %%edx\n\t");
    printf("idiv %%rcx\n\t");
  } else {
    printf("pop %%rcx\n\t");
    printf("%s %%rcx, %%rax\n\t", op);
  }
}

static void emit_expr(Ast *ast) {
  switch (ast->type) {
    case AST_LITERAL:
      switch (ast->ctype->type) {
        case CTYPE_INT:
          printf("mov $%d, %%eax\n\t", ast->ival);
          break;
        case CTYPE_CHAR:
          printf("mov $%d, %%rax\n\t", ast->c);
          break;
        default:
          error("internal error");
      }
      break;
    case AST_STRING:
      printf("lea %s(%%rip), %%rax\n\t", ast->slabel);
      break;
    case AST_LVAR:
      emit_lload(ast, 0);
      break;
    case AST_LREF:
      assert(ast->lref->type == AST_LVAR);
      emit_lload(ast->lref, ast->lrefoff);
      break;
    case AST_GVAR:
      emit_gload(ast->ctype, ast->glabel, 0);
      break;
    case AST_GREF: {
      if (ast->gref->type == AST_STRING) {
        printf("lea %s(%%rip), %%rax\n\t", ast->gref->slabel);
      } else {
        assert(ast->gref->type == AST_GVAR);
        emit_gload(ast->gref->ctype, ast->gref->glabel, ast->goff);
      }
      break;
    }
    case AST_FUNCALL: {
      for (int i = 1; i < list_len(ast->args); i++)
        printf("push %%%s\n\t", REGS[i]);
      for (Iter *i = list_iter(ast->args); !iter_end(i);) {
        emit_expr(iter_next(i));
        printf("push %%rax\n\t");
      }
      for (int i = list_len(ast->args) - 1; i >= 0; i--)
        printf("pop %%%s\n\t", REGS[i]);
      printf("mov $0, %%eax\n\t");
      printf("call %s\n\t", ast->fname);
      for (int i = list_len(ast->args) - 1; i > 0; i--)
        printf("pop %%%s\n\t", REGS[i]);
      break;
    }
    case AST_DECL: {
      if (ast->declinit->type == AST_ARRAY_INIT) {
        int i = 0;
        for (Iter *iter = list_iter(ast->declinit->arrayinit); !iter_end(iter);) {
          emit_expr(iter_next(iter));
          emit_lsave(ast->declvar->ctype->ptr, ast->declvar->loff, -i);
          i++;
        }
      } else if (ast->declvar->ctype->type == CTYPE_ARRAY) {
        assert(ast->declinit->type == AST_STRING);
        int i = 0;
        for (char *p = ast->declinit->sval; *p; p++, i++)
          printf("movb $%d, %d(%%rbp)\n\t", *p, -(ast->declvar->loff - i));
        printf("movb $0, %d(%%rbp)\n\t", -(ast->declvar->loff - i));
      } else if (ast->declinit->type == AST_STRING) {
        emit_gload(ast->declinit->ctype, ast->declinit->slabel, 0);
        emit_lsave(ast->declvar->ctype, ast->declvar->loff, 0);
      } else {
        emit_expr(ast->declinit);
        emit_lsave(ast->declvar->ctype, ast->declvar->loff, 0);
      }
      return;
    }
    case AST_ADDR:
      assert(ast->operand->type == AST_LVAR);
      printf("lea %d(%%rbp), %%rax\n\t", -ast->operand->loff);
      break;
    case AST_DEREF: {
      assert(ast->operand->ctype->type == CTYPE_PTR);
      emit_expr(ast->operand);
      char *reg;
      switch (ctype_size(ast->ctype)) {
        case 1: reg = "%cl";  break;
        case 4: reg = "%ecx"; break;
        case 8: reg = "%rcx"; break;
        default: error("internal error");
      }
      printf("mov $0, %%ecx\n\t");
      printf("mov (%%rax), %s\n\t", reg);
      printf("mov %%rcx, %%rax\n\t");
      break;
    }
    case AST_IF: {
      emit_expr(ast->cond);
      char *ne = make_label();
      printf("test %%rax, %%rax\n\t");
      printf("je %s\n\t", ne);
      emit_block(ast->then);
      if (ast->els) {
        char *end = make_label();
        printf("jmp %s\n\t", end);
        printf("%s:\n\t", ne);
        emit_block(ast->els);
        printf("%s:\n\t", end);
      } else {
        printf("%s:\n\t", ne);
      }
      break;
    }
    case AST_FOR: {
      if (ast->forinit)
        emit_expr(ast->forinit);
      char *begin = make_label();
      char *end = make_label();
      printf("%s:\n\t", begin);
      if (ast->forcond) {
        emit_expr(ast->forcond);
        printf("test %%rax, %%rax\n\t");
        printf("je %s\n\t", end);
      }
      emit_block(ast->forbody);
      if (ast->forstep)
        emit_expr(ast->forstep);
      printf("jmp %s\n\t", begin);
      printf("%s:\n\t", end);
      break;
    }
    case AST_RETURN:
      emit_expr(ast->retval);
      printf("leave\n\t"
             "ret\n");
      break;
    default:
      emit_binop(ast);
  }
}

void emit_data_section(void) {
  if (!globals) return;
  printf(".data\n");
  for (Iter *i = list_iter(globals); !iter_end(i);) {
    Ast *v = iter_next(i);
    assert(v->type == AST_STRING);
    printf("%s:\n\t", v->slabel);
    printf(".string \"%s\"\n", quote_cstring(v->sval));
  }
  printf("\t");
}

static int ceil8(int n) {
  int rem = n % 8;
  return (rem == 0) ? n : n - rem + 8;
}

static void emit_func_prologue(Ast *func) {
  if (list_len(func->params) > sizeof(REGS) / sizeof(*REGS))
    error("Parameter list too long: %s", func->fname);
  printf(".text\n\t"
         ".global %s\n"
         "%s:\n\t", func->fname, func->fname);
  printf("push %%rbp\n\t"
         "mov %%rsp, %%rbp\n\t");
  int off = 0;
  int ri = 0;
  for (Iter *i = list_iter(func->params); !iter_end(i); ri++) {
    printf("push %%%s\n\t", REGS[ri]);
    Ast *v = iter_next(i);
    off += ceil8(ctype_size(v->ctype));
    v->loff = off;
  }
  for (Iter *i = list_iter(func->locals); !iter_end(i);) {
    Ast *v = iter_next(i);
    off += ceil8(ctype_size(v->ctype));
    v->loff = off;
  }
  if (off)
    printf("sub $%d, %%rsp\n\t", off);
}

static void emit_func_epilogue(void) {
  printf("leave\n\t"
         "ret\n");
}

static void emit_block(List *block) {
  for (Iter *i = list_iter(block); !iter_end(i);)
    emit_expr(iter_next(i));
}

void emit_func(Ast *func) {
  assert(func->type == AST_FUNC);
  emit_func_prologue(func);
  emit_block(func->body);
  emit_func_epilogue();
}
