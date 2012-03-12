#include <stdarg.h>
#include <stdio.h>
#include "8cc.h"

static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static int TAB = 8;

static void emit_expr(Ast *ast);
static void emit_block(List *block);

#define emit(...)        emitf(__LINE__, "\t" __VA_ARGS__)
#define emit_label(...)  emitf(__LINE__, __VA_ARGS__)

void emitf(int line, char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int col = vprintf(fmt, args);
  va_end(args);

  for (char *p = fmt; *p; p++)
    if (*p == '\t')
      col += TAB - 1;
  int space = (30 - col) > 0 ? (30 - col) : 2;
  printf("%*c %d\n", space, '#', line);
}

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

static void emit_gload(Ctype *ctype, char *label) {
  if (ctype->type == CTYPE_ARRAY) {
    emit("lea %s(%%rip), %%rax", label);
    return;
  }
  char *reg;
  int size = ctype_size(ctype);
  switch (size) {
    case 1: reg = "al"; emit("mov $0, %%eax"); break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
    default:
      error("Unknown data size: %s: %d", ctype_to_string(ctype), size);
  }
  emit("mov %s(%%rip), %%%s", label, reg);
  emit("mov (%%rax), %%%s", reg);
}

static void emit_lload(Ast *var) {
  if (var->ctype->type == CTYPE_ARRAY) {
    emit("lea %d(%%rbp), %%rax", -var->loff);
    return;
  }
  int size = ctype_size(var->ctype);
  switch (size) {
    case 1:
      emit("mov $0, %%eax");
      emit("mov %d(%%rbp), %%al", -var->loff);
      break;
    case 4:
      emit("mov %d(%%rbp), %%eax", -var->loff);
      break;
    case 8:
      emit("mov %d(%%rbp), %%rax", -var->loff);
      break;
    default:
      error("Unknown data size: %s: %d", ast_to_string(var), size);
  }
}

static void emit_gsave(Ast *var) {
  assert(var->ctype->type != CTYPE_ARRAY);
  char *reg;
  emit("push %%rcx");
  emit("mov %s(%%rip), %%rcx", var->glabel);
  int size = ctype_size(var->ctype);
  switch (size) {
    case 1: reg = "al";  break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
    default:
      error("Unknown data size: %s: %d", ast_to_string(var), size);
  }
  emit("mov %s, (%%rbp)", reg);
  emit("pop %%rcx");
}

static void emit_lsave(Ctype *ctype, int loff, int off) {
  char *reg;
  int size = ctype_size(ctype);
  switch (size) {
    case 1: reg = "al";  break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
  }
  emit("mov %%%s, %d(%%rbp)", reg, -(loff + off * size));
}

static void emit_assign_deref(Ast *var) {
  emit("push %%rax");
  emit_expr(var->operand);
  emit("pop %%rcx");
  char *reg;
  int size = ctype_size(var->operand->ctype);
  switch (size) {
    case 1: reg = "cl";  break;
    case 4: reg = "ecx"; break;
    case 8: reg = "rcx"; break;
  }
  emit("mov %%%s, (%%rax)", reg);
}

static void emit_pointer_arith(char op, Ast *left, Ast *right) {
  emit_expr(left);
  emit("push %%rax");
  emit_expr(right);
  int size = ctype_size(left->ctype->ptr);
  if (size > 1)
    emit("imul $%d, %%rax", size);
  emit("mov %%rax, %%rcx");
  emit("pop %%rax");
  emit("add %%rcx, %%rax");
}

static void emit_assign(Ast *var) {
  if (var->type == AST_DEREF) {
    emit_assign_deref(var);
    return;
  }
  switch (var->type) {
    case AST_LVAR: emit_lsave(var->ctype, var->loff, 0); break;
    case AST_GVAR: emit_gsave(var); break;
    default: error("internal error");
  }
}

static void emit_comp(char *inst, Ast *a, Ast *b) {
  emit_expr(a);
  emit("push %%rax");
  emit_expr(b);
  emit("pop %%rcx");
  emit("cmp %%rax, %%rcx");
  emit("%s %%al", inst);
  emit("movzb %%al, %%eax");
}

static void emit_binop(Ast *ast) {
  if (ast->type == '=') {
    emit_expr(ast->right);
    emit_assign(ast->left);
    return;
  }
  if (ast->type == PUNCT_EQ) {
    emit_comp("sete", ast->left, ast->right);
    return;
  }
  if (ast->ctype->type == CTYPE_PTR) {
    emit_pointer_arith(ast->type, ast->left, ast->right);
    return;
  }
  char *op;
  switch (ast->type) {
    case '<':
      emit_comp("setl", ast->left, ast->right);
      return;
    case '>':
      emit_comp("setg", ast->left, ast->right);
      return;
    case '+': op = "add"; break;
    case '-': op = "sub"; break;
    case '*': op = "imul"; break;
    case '/': break;
    default: error("invalid operator '%d'", ast->type);
  }
  emit_expr(ast->right);
  emit("push %%rax");
  emit_expr(ast->left);
  if (ast->type == '/') {
    emit("pop %%rcx");
    emit("mov $0, %%edx");
    emit("idiv %%rcx");
  } else {
    emit("pop %%rcx");
    emit("%s %%rcx, %%rax", op);
  }
}

static void emit_inc_dec(Ast *ast, char *op) {
  emit_expr(ast->operand);
  emit("push %%rax");
  emit("%s $1, %%rax", op);
  emit_assign(ast->operand);
  emit("pop %%rax");
}

static void emit_expr(Ast *ast) {
  switch (ast->type) {
    case AST_LITERAL:
      switch (ast->ctype->type) {
        case CTYPE_INT:
          emit("mov $%d, %%eax", ast->ival);
          break;
        case CTYPE_CHAR:
          emit("mov $%d, %%rax", ast->c);
          break;
        default:
          error("internal error");
      }
      break;
    case AST_STRING:
      emit("lea %s(%%rip), %%rax", ast->slabel);
      break;
    case AST_LVAR:
      emit_lload(ast);
      break;
    case AST_GVAR:
      emit_gload(ast->ctype, ast->glabel);
      break;
    case AST_FUNCALL: {
      for (int i = 1; i < list_len(ast->args); i++)
        emit("push %%%s", REGS[i]);
      for (Iter *i = list_iter(ast->args); !iter_end(i);) {
        emit_expr(iter_next(i));
        emit("push %%rax");
      }
      for (int i = list_len(ast->args) - 1; i >= 0; i--)
        emit("pop %%%s", REGS[i]);
      emit("mov $0, %%eax");
      emit("call %s", ast->fname);
      for (int i = list_len(ast->args) - 1; i > 0; i--)
        emit("pop %%%s", REGS[i]);
      break;
    }
    case AST_DECL: {
      if (!ast->declinit)
        return;
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
          emit("movb $%d, %d(%%rbp)", *p, -(ast->declvar->loff - i));
        emit("movb $0, %d(%%rbp)", -(ast->declvar->loff - i));
      } else if (ast->declinit->type == AST_STRING) {
        emit_gload(ast->declinit->ctype, ast->declinit->slabel);
        emit_lsave(ast->declvar->ctype, ast->declvar->loff, 0);
      } else {
        emit_expr(ast->declinit);
        emit_lsave(ast->declvar->ctype, ast->declvar->loff, 0);
      }
      return;
    }
    case AST_ADDR:
      assert(ast->operand->type == AST_LVAR);
      emit("lea %d(%%rbp), %%rax", -ast->operand->loff);
      break;
    case AST_DEREF: {
      emit_expr(ast->operand);
      char *reg;
      switch (ctype_size(ast->ctype)) {
        case 1: reg = "%cl"; emit("mov $0, %%ecx"); break;
        case 4: reg = "%ecx"; break;
        default: reg = "%rcx"; break;
      }
      if (ast->operand->ctype->ptr->type != CTYPE_ARRAY) {
        emit("mov (%%rax), %s", reg);
        emit("mov %%rcx, %%rax");
      }
      break;
    }
    case AST_IF: {
      emit_expr(ast->cond);
      char *ne = make_label();
      emit("test %%rax, %%rax");
      emit("je %s", ne);
      emit_block(ast->then);
      if (ast->els) {
        char *end = make_label();
        emit("jmp %s", end);
        emit("%s:", ne);
        emit_block(ast->els);
        emit("%s:", end);
      } else {
        emit("%s:", ne);
      }
      break;
    }
    case AST_FOR: {
      if (ast->forinit)
        emit_expr(ast->forinit);
      char *begin = make_label();
      char *end = make_label();
      emit("%s:", begin);
      if (ast->forcond) {
        emit_expr(ast->forcond);
        emit("test %%rax, %%rax");
        emit("je %s", end);
      }
      emit_block(ast->forbody);
      if (ast->forstep)
        emit_expr(ast->forstep);
      emit("jmp %s", begin);
      emit("%s:", end);
      break;
    }
    case AST_RETURN:
      emit_expr(ast->retval);
      emit("leave");
      emit("ret");
      break;
    case PUNCT_INC:
      emit_inc_dec(ast, "add");
      break;
    case PUNCT_DEC:
      emit_inc_dec(ast, "sub");
      break;
    case '!':
      emit_expr(ast->operand);
      emit("cmp $0, %%rax");
      emit("sete %%al");
      emit("movzb %%al, %%eax");
      break;
    default:
      emit_binop(ast);
  }
}

void emit_data_section(void) {
  if (!globals) return;
  emit(".data");
  for (Iter *i = list_iter(globals); !iter_end(i);) {
    Ast *v = iter_next(i);
    assert(v->type == AST_STRING);
    emit_label("%s:", v->slabel);
    emit(".string \"%s\"", quote_cstring(v->sval));
  }
}

static int ceil8(int n) {
  int rem = n % 8;
  return (rem == 0) ? n : n - rem + 8;
}

static void emit_func_prologue(Ast *func) {
  if (list_len(func->params) > sizeof(REGS) / sizeof(*REGS))
    error("Parameter list too long: %s", func->fname);
  emit(".text");
  emit_label(".global %s", func->fname);
  emit_label("%s:", func->fname);
  emit("push %%rbp");
  emit("mov %%rsp, %%rbp");
  int off = 0;
  int ri = 0;
  for (Iter *i = list_iter(func->params); !iter_end(i); ri++) {
    emit("push %%%s", REGS[ri]);
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
    emit("sub $%d, %%rsp", off);
}

static void emit_func_epilogue(void) {
  emit("leave");
  emit("ret");
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
