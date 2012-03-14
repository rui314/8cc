#include <stdarg.h>
#include <stdio.h>
#include "8cc.h"

static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static int TAB = 8;

static void emit_expr(Ast *ast);
static void emit_load_deref(Ctype *result_type, Ctype *operand_type, int off);

#define emit(...)        emitf(__func__, __LINE__, "\t" __VA_ARGS__)
#define emit_label(...)  emitf(__func__, __LINE__, __VA_ARGS__)

void emitf(const char *func, int line, char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int col = vprintf(fmt, args);
  va_end(args);

  for (char *p = fmt; *p; p++)
    if (*p == '\t')
      col += TAB - 1;
  int space = (30 - col) > 0 ? (30 - col) : 2;
  printf("%*c %s:%d\n", space, '#', func, line);
}

int ctype_size(Ctype *ctype) {
  switch (ctype->type) {
    case CTYPE_CHAR: return 1;
    case CTYPE_INT:  return 4;
    case CTYPE_PTR:  return 8;
    case CTYPE_ARRAY:
      return ctype_size(ctype->ptr) * ctype->size;
    case CTYPE_STRUCT: {
      Ctype *last = list_last(ctype->fields);
      return last->offset + ctype_size(last);
    }
    default:
      error("internal error");
  }
}

static void emit_gload(Ctype *ctype, char *label, int off) {
  if (ctype->type == CTYPE_ARRAY) {
    if (off)
      emit("lea %s+%d(%%rip), %%rax", label, off);
    else
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
  if (off)
    emit("mov %s+%d(%%rip), %%%s", label, off, reg);
  else
    emit("mov %s(%%rip), %%%s", label, reg);
}

static void emit_lload(Ctype *ctype, int off) {
  if (ctype->type == CTYPE_ARRAY) {
    emit("lea %d(%%rbp), %%rax", -off);
    return;
  }
  int size = ctype_size(ctype);
  switch (size) {
    case 1:
      emit("mov $0, %%eax");
      emit("mov %d(%%rbp), %%al", -off);
      break;
    case 4:
      emit("mov %d(%%rbp), %%eax", -off);
      break;
    case 8:
      emit("mov %d(%%rbp), %%rax", -off);
      break;
    default:
      error("Unknown data size: %s: %d", ctype_to_string(ctype), size);
  }
}

static void emit_gsave(char *varname, Ctype *ctype, int off) {
  assert(ctype->type != CTYPE_ARRAY);
  char *reg;
  int size = ctype_size(ctype);
  switch (size) {
    case 1: reg = "al";  break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
    default:
      error("Unknown data size: %s: %d", ctype_to_string(ctype), size);
  }
  if (off)
    emit("mov %%%s, %s+%d(%%rip)", reg, varname, off);
  else
    emit("mov %%%s, %s(%%rip)", reg, varname);
}

static void emit_lsave(Ctype *ctype, int off) {
  char *reg;
  int size = ctype_size(ctype);
  switch (size) {
    case 1: reg = "al";  break;
    case 4: reg = "eax"; break;
    case 8: reg = "rax"; break;
  }
  emit("mov %%%s, %d(%%rbp)", reg, -off);
}

static void emit_assign_deref_int(Ctype *ctype, int off) {
  char *reg;
  emit("pop %%rcx");
  int size = ctype_size(ctype);
  switch (size) {
    case 1: reg = "cl";  break;
    case 4: reg = "ecx"; break;
    case 8: reg = "rcx"; break;
  }
  if (off)
    emit("mov %%%s, %d(%%rax)", reg, off);
  else
    emit("mov %%%s, (%%rax)", reg);
}

static void emit_assign_deref(Ast *var) {
  emit("push %%rax");
  emit_expr(var->operand);
  emit_assign_deref_int(var->operand->ctype, 0);
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

static void emit_assign_struct_ref(Ast *struc, Ctype *field, int off) {
  switch (struc->type) {
    case AST_LVAR:
      emit_lsave(field, struc->loff - field->offset - off);
      break;
    case AST_GVAR:
      emit_gsave(struc->varname, field, field->offset + off);
      break;
    case AST_STRUCT_REF:
      emit_assign_struct_ref(struc->struc, field, off + struc->field->offset);
      break;
    case AST_DEREF:
      emit("push %%rax");
      emit_expr(struc->operand);
      emit_assign_deref_int(field, field->offset + off);
      break;
    default:
      error("internal error: %s", ast_to_string(struc));
  }
}

static void emit_load_struct_ref(Ast *struc, Ctype *field, int off) {
  switch (struc->type) {
    case AST_LVAR:
      emit_lload(field, struc->loff - field->offset - off);
      break;
    case AST_GVAR:
      emit_gload(field, struc->varname, field->offset + off);
      break;
    case AST_STRUCT_REF:
      emit_load_struct_ref(struc->struc, field, struc->field->offset + off);
      break;
    case AST_DEREF:
      emit_expr(struc->operand);
      emit_load_deref(struc->ctype, field, field->offset + off);
      break;
    default:
      error("internal error: %s", ast_to_string(struc));
  }
}

static void emit_assign(Ast *var) {
  switch (var->type) {
    case AST_DEREF: emit_assign_deref(var); break;
    case AST_STRUCT_REF: emit_assign_struct_ref(var->struc, var->field, 0); break;
    case AST_LVAR: emit_lsave(var->ctype, var->loff); break;
    case AST_GVAR: emit_gsave(var->varname, var->ctype, 0); break;
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
  emit_expr(ast->left);
  emit("push %%rax");
  emit_expr(ast->right);
  emit("mov %%rax, %%rcx");
  if (ast->type == '/') {
    emit("pop %%rax");
    emit("mov $0, %%edx");
    emit("idiv %%rcx");
  } else {
    emit("pop %%rax");
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

static void emit_load_deref(Ctype *result_type, Ctype *operand_type, int off) {
  if (operand_type->type == CTYPE_PTR &&
      operand_type->ptr->type == CTYPE_ARRAY)
    return;
  char *reg;
  switch (ctype_size(result_type)) {
    case 1: reg = "%cl"; emit("mov $0, %%ecx"); break;
    case 4: reg = "%ecx"; break;
    default: reg = "%rcx"; break;
  }
  if (off)
    emit("mov %d(%%rax), %s", off, reg);
  else
    emit("mov (%%rax), %s", reg);
  emit("mov %%rcx, %%rax");
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
      emit_lload(ast->ctype, ast->loff);
      break;
    case AST_GVAR:
      emit_gload(ast->ctype, ast->glabel, 0);
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
        int off = 0;
        for (Iter *iter = list_iter(ast->declinit->arrayinit); !iter_end(iter);) {
          emit_expr(iter_next(iter));
          emit_lsave(ast->declvar->ctype->ptr, ast->declvar->loff - off);
          off += ctype_size(ast->declvar->ctype->ptr);
        }
      } else if (ast->declvar->ctype->type == CTYPE_ARRAY) {
        assert(ast->declinit->type == AST_STRING);
        int i = 0;
        for (char *p = ast->declinit->sval; *p; p++, i++)
          emit("movb $%d, %d(%%rbp)", *p, -(ast->declvar->loff - i));
        emit("movb $0, %d(%%rbp)", -(ast->declvar->loff - i));
      } else if (ast->declinit->type == AST_STRING) {
        emit_gload(ast->declinit->ctype, ast->declinit->slabel, 0);
        emit_lsave(ast->declvar->ctype, ast->declvar->loff);
      } else {
        emit_expr(ast->declinit);
        emit_lsave(ast->declvar->ctype, ast->declvar->loff);
      }
      return;
    }
    case AST_ADDR:
      switch (ast->operand->type) {
        case AST_LVAR:
          emit("lea %d(%%rbp), %%rax", -ast->operand->loff);
          break;
        case AST_GVAR:
          emit("lea %s(%%rip), %%rax", ast->operand->glabel);
          break;
        default:
          error("internal error");
      }
      break;
    case AST_DEREF:
      emit_expr(ast->operand);
      emit_load_deref(ast->ctype, ast->operand->ctype, 0);
      break;
    case AST_IF:
    case AST_TERNARY: {
      emit_expr(ast->cond);
      char *ne = make_label();
      emit("test %%rax, %%rax");
      emit("je %s", ne);
      emit_expr(ast->then);
      if (ast->els) {
        char *end = make_label();
        emit("jmp %s", end);
        emit("%s:", ne);
        emit_expr(ast->els);
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
      emit_expr(ast->forbody);
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
    case AST_COMPOUND_STMT:
      for (Iter *i = list_iter(ast->stmts); !iter_end(i);)
        emit_expr(iter_next(i));
      break;
    case AST_STRUCT_REF:
      emit_load_struct_ref(ast->struc, ast->field, 0);
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
    case '&':
      emit_expr(ast->left);
      emit("push %%rax");
      emit_expr(ast->right);
      emit("pop %%rcx");
      emit("and %%rcx, %%rax");
      break;
    case '|':
      emit_expr(ast->left);
      emit("push %%rax");
      emit_expr(ast->right);
      emit("pop %%rcx");
      emit("or %%rcx, %%rax");
      break;
    case PUNCT_LOGAND: {
      char *end = make_label();
      emit_expr(ast->left);
      emit("test %%rax, %%rax");
      emit("mov $0, %%rax");
      emit("je %s", end);
      emit_expr(ast->right);
      emit("test %%rax, %%rax");
      emit("mov $0, %%rax");
      emit("je %s", end);
      emit("mov $1, %%rax");
      emit("%s:", end);
      break;
    }
    case PUNCT_LOGOR: {
      char *end = make_label();
      emit_expr(ast->left);
      emit("test %%rax, %%rax");
      emit("mov $1, %%rax");
      emit("jne %s", end);
      emit_expr(ast->right);
      emit("test %%rax, %%rax");
      emit("mov $1, %%rax");
      emit("jne %s", end);
      emit("mov $0, %%rax");
      emit("%s:", end);
      break;
    }
    default:
      emit_binop(ast);
  }
}

void emit_data_section(void) {
  if (list_len(globalenv->vars) == 0) return;
  emit(".data");
  for (Iter *i = list_iter(globalenv->vars); !iter_end(i);) {
    Ast *v = iter_next(i);
    if (v->type == AST_STRING) {
      emit_label("%s:", v->slabel);
      emit(".string \"%s\"", quote_cstring(v->sval));
    } else if (v->type != AST_GVAR) {
      error("internal error: %s", ast_to_string(v));
    }
  }
}

static int ceil8(int n) {
  int rem = n % 8;
  return (rem == 0) ? n : n - rem + 8;
}

static void emit_data_int(Ast *data) {
  assert(data->ctype->type != CTYPE_ARRAY);
  switch (ctype_size(data->ctype)) {
    case 1: emit(".byte %d", data->ival); break;
    case 4: emit(".long %d", data->ival); break;
    case 8: emit(".quad %d", data->ival); break;
    default: error("internal error");
  }
}

static void emit_data(Ast *v) {
  emit_label(".global %s", v->declvar->varname);
  emit_label("%s:", v->declvar->varname);
  if (v->declinit->type == AST_ARRAY_INIT) {
    for (Iter *iter = list_iter(v->declinit->arrayinit); !iter_end(iter);) {
      emit_data_int(iter_next(iter));
    }
    return;
  }
  assert(v->declinit->type == AST_LITERAL && v->declinit->ctype->type == CTYPE_INT);
  emit_data_int(v->declinit);
}

static void emit_bss(Ast *v) {
  emit(".lcomm %s, %d", v->declvar->varname, ctype_size(v->declvar->ctype));
}

static void emit_global_var(Ast *v) {
  if (v->declinit)
    emit_data(v);
  else
    emit_bss(v);
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
  for (Iter *i = list_iter(func->localvars); !iter_end(i);) {
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

void emit_toplevel(Ast *v) {
  if (v->type == AST_FUNC) {
    emit_func_prologue(v);
    emit_expr(v->body);
    emit_func_epilogue();
  } else if (v->type == AST_DECL) {
    emit_global_var(v);
  } else {
    error("internal error");
  }
}
