// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"
#include "list.h"

static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char *SREGS[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *MREGS[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
static int TAB = 8;
static List *functions = &EMPTY_LIST;
static char *lbreak;
static char *lcontinue;
static char *lswitch;
static int stackpos;
static int numgp;
static int numfp;
static FILE *outputfp;

static void emit_expr(Node *node);
static void emit_decl_init(List *inits, int off);
static void emit_data_int(List *inits, int size, int off, int depth);
static void emit_data(Node *v, int off, int depth);

#define REGAREA_SIZE 304

#define emit(...)        emitf(__LINE__, "\t" __VA_ARGS__)
#define emit_noindent(...)  emitf(__LINE__, __VA_ARGS__)

#ifdef __GNUC__
#define SAVE                                                            \
    int save_hook __attribute__((unused, cleanup(pop_function)));       \
    list_push(functions, (void *)__func__)

static void pop_function(void *ignore) {
    list_pop(functions);
}
#else
#define SAVE
#endif

static char *get_caller_list(void) {
    String *s = make_string();
    for (Iter *i = list_iter(functions); !iter_end(i);) {
        string_appendf(s, "%s", iter_next(i));
        if (!iter_end(i))
            string_appendf(s, " -> ");
    }
    return get_cstring(s);
}

void set_output_file(FILE *fp) {
    outputfp = fp;
}

void close_output_file(void) {
    fclose(outputfp);
}

static void emitf(int line, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int col = vfprintf(outputfp, fmt, args);
    va_end(args);

    for (char *p = fmt; *p; p++)
        if (*p == '\t')
            col += TAB - 1;
    int space = (28 - col) > 0 ? (30 - col) : 2;
    fprintf(outputfp, "%*c %s:%d\n", space, '#', get_caller_list(), line);
}

static char *get_int_reg(Ctype *ctype, char r) {
    assert(r == 'a' || r == 'c');
    switch (ctype->size) {
    case 1: return (r == 'a') ? "al" : "cl";
    case 2: return (r == 'a') ? "ax" : "cx";
    case 4: return (r == 'a') ? "eax" : "ecx";
    case 8: return (r == 'a') ? "rax" : "rcx";
    default:
        error("Unknown data size: %s: %d", c2s(ctype), ctype->size);
    }
}

static char *get_load_inst(Ctype *ctype) {
    switch (ctype->size) {
    case 1: return "movsbq";
    case 2: return "movswq";
    case 4: return "movslq";
    case 8: return "mov";
    default:
        error("Unknown data size: %s: %d", c2s(ctype), ctype->size);
    }
}

static void push_xmm(int reg) {
    SAVE;
    emit("sub $8, %%rsp");
    emit("movsd %%xmm%d, (%%rsp)", reg);
    stackpos += 8;
}

static void pop_xmm(int reg) {
    SAVE;
    emit("movsd (%%rsp), %%xmm%d", reg);
    emit("add $8, %%rsp");
    stackpos -= 8;
    assert(stackpos >= 0);
}

static void push(char *reg) {
    SAVE;
    emit("push %%%s", reg);
    stackpos += 8;
}

static void pop(char *reg) {
    SAVE;
    emit("pop %%%s", reg);
    stackpos -= 8;
    assert(stackpos >= 0);
}

static void maybe_emit_bitshift_load(Ctype *ctype) {
    SAVE;
    if (ctype->bitsize <= 0)
        return;
    emit("shr $%d, %%rax", ctype->bitoff);
    push("rcx");
    emit("mov $0x%lx, %%rcx", (1 << (long)ctype->bitsize) - 1);
    emit("and %%rcx, %%rax");
    pop("rcx");
}

static void maybe_emit_bitshift_save(Ctype *ctype, char *addr) {
    SAVE;
    if (ctype->bitsize <= 0)
        return;
    push("rcx");
    push("rdi");
    emit("mov $0x%lx, %%rdi", (1 << (long)ctype->bitsize) - 1);
    emit("and %%rdi, %%rax");
    emit("shl $%d, %%rax", ctype->bitoff);
    emit("mov %s, %%%s", addr, get_int_reg(ctype, 'c'));
    emit("mov $0x%lx, %%rdi", ~(((1 << (long)ctype->bitsize) - 1) << ctype->bitoff));
    emit("and %%rdi, %%rcx");
    emit("or %%rcx, %%rax");
    pop("rdi");
    pop("rcx");
}

static void emit_gload(Ctype *ctype, char *label, int off) {
    SAVE;
    if (ctype->type == CTYPE_ARRAY) {
        if (off)
            emit("lea %s+%d(%%rip), %%rax", label, off);
        else
            emit("lea %s(%%rip), %%rax", label);
        return;
    }
    char *inst = get_load_inst(ctype);
    emit("%s %s+%d(%%rip), %%rax", inst, label, off);
    maybe_emit_bitshift_load(ctype);
}

static void emit_toint(Ctype *ctype) {
    SAVE;
    if (ctype->type == CTYPE_FLOAT)
        emit("cvttss2si %%xmm0, %%eax");
    else if (ctype->type == CTYPE_FLOAT)
        emit("cvttsd2si %%xmm0, %%eax");
}

static void emit_lload(Ctype *ctype, char *base, int off) {
    SAVE;
    if (ctype->type == CTYPE_ARRAY) {
        emit("lea %d(%%%s), %%rax", off, base);
    } else if (ctype->type == CTYPE_FLOAT) {
        emit("movss %d(%%%s), %%xmm0", off, base);
    } else if (ctype->type == CTYPE_DOUBLE || ctype->type == CTYPE_LDOUBLE) {
        emit("movsd %d(%%%s), %%xmm0", off, base);
    } else {
        char *inst = get_load_inst(ctype);
        emit("%s %d(%%%s), %%rax", inst, off, base);
        maybe_emit_bitshift_load(ctype);
    }
}

static void maybe_convert_bool(Ctype *ctype) {
    if (ctype->type == CTYPE_BOOL) {
        emit("test %%rax, %%rax");
        emit("setne %%al");
    }
}

static void emit_gsave(char *varname, Ctype *ctype, int off) {
    SAVE;
    assert(ctype->type != CTYPE_ARRAY);
    maybe_convert_bool(ctype);
    char *reg = get_int_reg(ctype, 'a');
    char *addr = format("%s+%d(%%rip)", varname, off);
    maybe_emit_bitshift_save(ctype, addr);
    emit("mov %%%s, %s", reg, addr);
}

static void emit_lsave(Ctype *ctype, int off) {
    SAVE;
    if (ctype->type == CTYPE_FLOAT) {
        emit("movss %%xmm0, %d(%%rbp)", off);
    } else if (ctype->type == CTYPE_DOUBLE) {
        emit("movsd %%xmm0, %d(%%rbp)", off);
    } else {
        maybe_convert_bool(ctype);
        char *reg = get_int_reg(ctype, 'a');
        char *addr = format("%d(%%rbp)", off);
        maybe_emit_bitshift_save(ctype, addr);
        emit("mov %%%s, %s", reg, addr);
    }
}

static void emit_assign_deref_int(Ctype *ctype, int off) {
    SAVE;
    emit("mov (%%rsp), %%rcx");
    char *reg = get_int_reg(ctype, 'c');
    if (off)
        emit("mov %%%s, %d(%%rax)", reg, off);
    else
        emit("mov %%%s, (%%rax)", reg);
    pop("rax");
}

static void emit_assign_deref(Node *var) {
    SAVE;
    push("rax");
    emit_expr(var->operand);
    emit_assign_deref_int(var->operand->ctype->ptr, 0);
}

static void emit_pointer_arith(char type, Node *left, Node *right) {
    SAVE;
    emit_expr(left);
    push("rcx");
    push("rax");
    emit_expr(right);
    int size = left->ctype->ptr->size;
    if (size > 1)
        emit("imul $%d, %%rax", size);
    emit("mov %%rax, %%rcx");
    pop("rax");
    switch (type) {
    case '+': emit("add %%rcx, %%rax"); break;
    case '-': emit("sub %%rcx, %%rax"); break;
    default: error("invalid operator '%d'", type);
    }
    pop("rcx");
}

static void emit_zero_filler(int start, int end) {
    for (; start <= end - 4; start += 4)
        emit("movl $0, %d(%%rbp)", start);
    for (; start < end; start++)
        emit("movb $0, %d(%%rbp)", start);
}

static void ensure_lvar_init(Node *node) {
    assert(node->type == AST_LVAR);
    if (node->lvarinit) {
        emit_zero_filler(node->loff, node->loff + node->ctype->size);
        emit_decl_init(node->lvarinit, node->loff);
    }
    node->lvarinit = NULL;
}

static void emit_assign_struct_ref(Node *struc, Ctype *field, int off) {
    SAVE;
    switch (struc->type) {
    case AST_LVAR:
        ensure_lvar_init(struc);
        emit_lsave(field, struc->loff + field->offset + off);
        break;
    case AST_GVAR:
        emit_gsave(struc->varname, field, field->offset + off);
        break;
    case AST_STRUCT_REF:
        emit_assign_struct_ref(struc->struc, field, off + struc->ctype->offset);
        break;
    case AST_DEREF:
        push("rax");
        emit_expr(struc->operand);
        emit_assign_deref_int(field, field->offset + off);
        break;
    default:
        error("internal error: %s", a2s(struc));
    }
}

static void emit_load_struct_ref(Node *struc, Ctype *field, int off) {
    SAVE;
    switch (struc->type) {
    case AST_LVAR:
        ensure_lvar_init(struc);
        emit_lload(field, "rbp", struc->loff + field->offset + off);
        break;
    case AST_GVAR:
        emit_gload(field, struc->varname, field->offset + off);
        break;
    case AST_STRUCT_REF:
        emit_load_struct_ref(struc->struc, field, struc->ctype->offset + off);
        break;
    case AST_DEREF:
        emit_expr(struc->operand);
        emit_lload(field, "rax", field->offset + off);
        break;
    default:
        error("internal error: %s", a2s(struc));
    }
}

static void emit_store(Node *var) {
    SAVE;
    switch (var->type) {
    case AST_DEREF: emit_assign_deref(var); break;
    case AST_STRUCT_REF: emit_assign_struct_ref(var->struc, var->ctype, 0); break;
    case AST_LVAR:
        ensure_lvar_init(var);
        emit_lsave(var->ctype, var->loff);
        break;
    case AST_GVAR: emit_gsave(var->varname, var->ctype, 0); break;
    default: error("internal error");
    }
}

static void emit_to_bool(Ctype *ctype) {
    SAVE;
    if (is_flotype(ctype)) {
        push_xmm(1);
        emit("xorpd %%xmm1, %%xmm1");
        emit("%s %%xmm1, %%xmm0", (ctype->type == CTYPE_FLOAT) ? "ucomiss" : "ucomisd");
        emit("setne %%al");
        pop_xmm(1);
    } else {
        emit("cmp $0, %%rax");
        emit("setne %%al");
    }
    emit("movzb %%al, %%eax");
}

static void emit_comp(char *inst, Node *node) {
    SAVE;
    if (is_flotype(node->left->ctype)) {
        emit_expr(node->left);
        push_xmm(0);
        emit_expr(node->right);
        pop_xmm(1);
        if (node->left->ctype->type == CTYPE_FLOAT)
            emit("ucomiss %%xmm0, %%xmm1");
        else
            emit("ucomisd %%xmm0, %%xmm1");
    } else {
        emit_expr(node->left);
        push("rax");
        emit_expr(node->right);
        pop("rcx");
        int type = node->left->ctype->type;
        if (type == CTYPE_LONG || type == CTYPE_LLONG)
          emit("cmp %%rax, %%rcx");
        else
          emit("cmp %%eax, %%ecx");
    }
    emit("%s %%al", inst);
    emit("movzb %%al, %%eax");
}

static void emit_binop_int_arith(Node *node) {
    SAVE;
    char *op = NULL;
    switch (node->type) {
    case '+': op = "add"; break;
    case '-': op = "sub"; break;
    case '*': op = "imul"; break;
    case '^': op = "xor"; break;
    case OP_SAL: op = "sal"; break;
    case OP_SAR: op = "sar"; break;
    case OP_SHR: op = "shr"; break;
    case '/': case '%': break;
    default: error("invalid operator '%d'", node->type);
    }
    emit_expr(node->left);
    push("rax");
    emit_expr(node->right);
    emit("mov %%rax, %%rcx");
    pop("rax");
    if (node->type == '/' || node->type == '%') {
        emit("cqto");
        emit("idiv %%rcx");
        if (node->type == '%')
            emit("mov %%edx, %%eax");
    } else if (node->type == OP_SAL || node->type == OP_SAR || node->type == OP_SHR) {
        emit("%s %%cl, %%%s", op, get_int_reg(node->left->ctype, 'a'));
    } else {
        emit("%s %%rcx, %rax", op);
    }
}

static void emit_binop_float_arith(Node *node) {
    SAVE;
    char *op;
    bool isdouble = (node->ctype->type == CTYPE_DOUBLE);
    switch (node->type) {
    case '+': op = (isdouble ? "addsd" : "addss"); break;
    case '-': op = (isdouble ? "subsd" : "subss"); break;
    case '*': op = (isdouble ? "mulsd" : "mulss"); break;
    case '/': op = (isdouble ? "divsd" : "divss"); break;
    default: error("invalid operator '%d'", node->type);
    }
    emit_expr(node->left);
    push_xmm(0);
    emit_expr(node->right);
    emit("%s %%xmm0, %%xmm1", (isdouble ? "movsd" : "movss"));
    pop_xmm(0);
    emit("%s %%xmm1, %%xmm0", op);
}

static void emit_load_convert(Ctype *to, Ctype *from) {
    SAVE;
    if (is_inttype(from) && to->type == CTYPE_FLOAT)
        emit("cvtsi2ss %%eax, %%xmm0");
    else if (is_inttype(from) && to->type == CTYPE_DOUBLE)
        emit("cvtsi2sd %%eax, %%xmm0");
    else if (from->type == CTYPE_FLOAT && to->type == CTYPE_DOUBLE)
        emit("cvtps2pd %%xmm0, %%xmm0");
    else if (from->type == CTYPE_DOUBLE && to->type == CTYPE_FLOAT)
        emit("cvtpd2ps %%xmm0, %%xmm0");
    else if (to->type == CTYPE_BOOL)
        emit_to_bool(from);
    else if (is_inttype(to))
        emit_toint(from);
}

static void emit_ret(void) {
    SAVE;
    emit("leave");
    emit("ret");
}

static void emit_binop(Node *node) {
    SAVE;
    if (node->ctype->type == CTYPE_PTR) {
        emit_pointer_arith(node->type, node->left, node->right);
        return;
    }
    switch (node->type) {
    case '<': emit_comp("setl", node); return;
    case '>': emit_comp("setg", node); return;
    case OP_EQ: emit_comp("sete", node); return;
    case OP_GE: emit_comp("setge", node); return;
    case OP_LE: emit_comp("setle", node); return;
    case OP_NE: emit_comp("setne", node); return;
    }
    if (is_inttype(node->ctype))
        emit_binop_int_arith(node);
    else if (is_flotype(node->ctype))
        emit_binop_float_arith(node);
    else
        error("internal error");
}

static void emit_save_literal(Node *node, Ctype *totype, int off) {
    switch (totype->type) {
    case CTYPE_BOOL:  emit("movb $%d, %d(%%rbp)", !!node->ival, off); break;
    case CTYPE_CHAR:  emit("movb $%d, %d(%%rbp)", node->ival, off); break;
    case CTYPE_SHORT: emit("movw $%d, %d(%%rbp)", node->ival, off); break;
    case CTYPE_INT:   emit("movl $%d, %d(%%rbp)", node->ival, off); break;
    case CTYPE_LONG:
    case CTYPE_LLONG:
    case CTYPE_PTR: {
        unsigned long ival = node->ival;
        emit("movl $%lu, %d(%%rbp)", ival & ((1L << 32) - 1), off);
        emit("movl $%lu, %d(%%rbp)", ival >> 32, off + 4);
        break;
    }
    case CTYPE_FLOAT: {
        float fval = node->fval;
        int *p = (int *)&fval;
        emit("movl $%u, %d(%%rbp)", *p, off);
        break;
    }
    case CTYPE_DOUBLE: {
        long *p = (long *)&node->fval;
        emit("movl $%lu, %d(%%rbp)", *p & ((1L << 32) - 1), off);
        emit("movl $%lu, %d(%%rbp)", *p >> 32, off + 4);
        break;
    }
    default:
        error("internal error: <%s> <%s> <%d>", a2s(node), c2s(totype), off);
    }
}

static void emit_addr(Node *node) {
    switch (node->type) {
    case AST_LVAR:
        ensure_lvar_init(node);
        emit("lea %d(%%rbp), %%rax", node->loff);
        break;
    case AST_GVAR:
        emit("lea %s(%%rip), %%rax", node->glabel);
        break;
    case AST_DEREF:
        emit_expr(node->operand);
        break;
    case AST_STRUCT_REF:
        emit_addr(node->struc);
        emit("add $%d, %%rax", node->ctype->offset);
        break;
    default:
        error("internal error: %s", a2s(node));
    }
}

static void emit_copy_struct(Node *left, Node *right) {
    push("rcx");
    push("r11");
    emit_addr(right);
    emit("mov %%rax, %%rcx");
    emit_addr(left);
    int i = 0;
    for (; i < left->ctype->size; i += 8) {
        emit("movq %d(%%rcx), %%r11", i);
        emit("movq %%r11, %d(%%rax)", i);
    }
    for (; i < left->ctype->size; i += 4) {
        emit("movl %d(%%rcx), %%r11", i);
        emit("movl %%r11, %d(%%rax)", i);
    }
    for (; i < left->ctype->size; i++) {
        emit("movb %d(%%rcx), %%r11", i);
        emit("movb %%r11, %d(%%rax)", i);
    }
    pop("r11");
    pop("rcx");
}

static void emit_decl_init(List *inits, int off) {
    Iter *iter = list_iter(inits);
    while (!iter_end(iter)) {
        Node *node = iter_next(iter);
        assert(node->type == AST_INIT);
        if (node->initval->type == AST_LITERAL &&
            node->totype->bitsize <= 0) {
            emit_save_literal(node->initval, node->totype, node->initoff + off);
        } else {
            emit_expr(node->initval);
            emit_lsave(node->totype, node->initoff + off);
        }
    }
}

static void emit_uminus(Node *node) {
    emit_expr(node->operand);
    if (is_flotype(node->ctype)) {
        push_xmm(1);
        emit("xorpd %%xmm1, %%xmm1");
        emit("%s %%xmm1, %%xmm0", (node->ctype->type == CTYPE_DOUBLE ? "subsd" : "subss"));
        pop_xmm(1);
    } else {
        emit("neg %%rax");
    }
}

static void emit_pre_inc_dec(Node *node, char *op) {
    emit_expr(node->operand);
    emit("%s $1, %%rax", op);
    emit_store(node->operand);
}

static void emit_post_inc_dec(Node *node, char *op) {
    SAVE;
    emit_expr(node->operand);
    push("rax");
    emit("%s $1, %%rax", op);
    emit_store(node->operand);
    pop("rax");
}

static void set_reg_nums(List *args) {
    numgp = numfp = 0;
    for (Iter *i = list_iter(args); !iter_end(i);) {
        Node *arg = iter_next(i);
        if (is_flotype(arg->ctype))
            numfp++;
        else
            numgp++;
    }
}

static void emit_je(char *label) {
    emit("test %%rax, %%rax");
    emit("je %s", label);
}

static void emit_label(char *label) {
    emit("%s:", label);
}

static void emit_jmp(char *label) {
    emit("jmp %s", label);
}

static void emit_literal(Node *node) {
    SAVE;
    switch (node->ctype->type) {
    case CTYPE_BOOL:
    case CTYPE_CHAR:
        emit("mov $%d, %%rax", node->ival);
        break;
    case CTYPE_INT:
        emit("mov $%d, %%rax", node->ival);
        break;
    case CTYPE_LONG:
    case CTYPE_LLONG: {
        emit("mov $%lu, %%rax", node->ival);
        break;
    }
    case CTYPE_FLOAT: {
        if (!node->flabel) {
            node->flabel = make_label();
            float fval = node->fval;
            int *p = (int *)&fval;
            emit_noindent(".data");
            emit_label(node->flabel);
            emit(".long %d", *p);
            emit_noindent(".text");
        }
        emit("movss %s(%%rip), %%xmm0", node->flabel);
        break;
    }
    case CTYPE_DOUBLE:
    case CTYPE_LDOUBLE: {
        if (!node->flabel) {
            node->flabel = make_label();
            int *fval = (int *)&node->fval;
            emit_noindent(".data");
            emit_label(node->flabel);
            emit(".long %d", fval[0]);
            emit(".long %d", fval[1]);
            emit_noindent(".text");
        }
        emit("movsd %s(%%rip), %%xmm0", node->flabel);
        break;
    }
    default:
        error("internal error");
    }
}

static void emit_literal_string(Node *node) {
    SAVE;
    if (!node->slabel) {
        node->slabel = make_label();
        emit_noindent(".data");
        emit_label(node->slabel);
        emit(".string \"%s\"", quote_cstring(node->sval));
        emit_noindent(".text");
    }
    emit("lea %s(%%rip), %%rax", node->slabel);
}

static void emit_lvar(Node *node) {
    SAVE;
    ensure_lvar_init(node);
    emit_lload(node->ctype, "rbp", node->loff);
}

static void emit_gvar(Node *node) {
    SAVE;
    emit_gload(node->ctype, node->glabel, 0);
}

static void classify_args(List *ints, List *floats, List *rest, List *args) {
    SAVE;
    int ireg = 0, xreg = 0;
    int imax = 6, xmax = 8;
    Iter *iter = list_iter(args);
    while (!iter_end(iter)) {
        Node *v = iter_next(iter);
        if (is_flotype(v->ctype))
            list_push((xreg++ < xmax) ? floats : rest, v);
        else
            list_push((ireg++ < imax) ? ints : rest, v);
    }
}

static void save_arg_regs(int nints, int nfloats) {
    SAVE;
    assert(nints <= 6);
    assert(nfloats <= 8);
    for (int i = 0; i < nints; i++)
        push(REGS[i]);
    for (int i = 1; i < nfloats; i++)
        push_xmm(i);
}

static void restore_arg_regs(int nints, int nfloats) {
    SAVE;
    for (int i = nfloats - 1; i > 0; i--)
        pop_xmm(i);
    for (int i = nints - 1; i >= 0; i--)
        pop(REGS[i]);
}

static void emit_args(List *vals) {
    SAVE;
    Iter *iter = list_iter(vals);
    while (!iter_end(iter)) {
        Node *v = iter_next(iter);
        emit_expr(v);
        if (is_flotype(v->ctype))
            push_xmm(0);
        else
            push("rax");
    }
}

static void pop_int_args(int nints) {
    SAVE;
    for (int i = nints - 1; i >= 0; i--)
        pop(REGS[i]);
}

static void pop_float_args(int nfloats) {
    SAVE;
    for (int i = nfloats - 1; i >= 0; i--)
        pop_xmm(i);
}

static void maybe_booleanize_retval(Ctype *ctype) {
    if (ctype->type == CTYPE_BOOL) {
        emit("movzx %%al, %%rax");
    }
}

static void emit_func_call(Node *node) {
    SAVE;
    int opos = stackpos;
    bool isptr = (node->type == AST_FUNCPTR_CALL);
    Ctype *ftype = isptr ? node->fptr->ctype->ptr : node->ftype;

    List *ints = make_list();
    List *floats = make_list();
    List *rest = make_list();
    classify_args(ints, floats, rest, node->args);
    save_arg_regs(list_len(ints), list_len(floats));

    bool padding = stackpos % 16;
    if (padding) {
        emit("sub $8, %%rsp");
        stackpos += 8;
    }

    emit_args(list_reverse(rest));
    if (isptr) {
        emit_expr(node->fptr);
        push("rax");
    }
    emit_args(ints);
    emit_args(floats);
    pop_float_args(list_len(floats));
    pop_int_args(list_len(ints));

    if (isptr) pop("r11");
    if (ftype->hasva)
        emit("mov $%d, %%eax", list_len(floats));

    if (isptr)
        emit("call *%%r11");
    else
        emit("call %s", node->fname);
    maybe_booleanize_retval(node->ctype);
    if (list_len(rest) > 0) {
        emit("add $%d, %%rsp", list_len(rest) * 8);
        stackpos -= list_len(rest) * 8;
    }
    if (padding) {
        emit("add $8, %%rsp");
        stackpos -= 8;
    }
    restore_arg_regs(list_len(ints), list_len(floats));
    assert(opos == stackpos);
}

static void emit_decl(Node *node) {
    SAVE;
    if (!node->declinit)
        return;
    emit_zero_filler(node->declvar->loff,
                     node->declvar->loff + node->declvar->ctype->size);
    emit_decl_init(node->declinit, node->declvar->loff);
}

static void emit_conv(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_load_convert(node->ctype, node->operand->ctype);
}

static void emit_deref(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_lload(node->operand->ctype->ptr, "rax", 0);
    emit_load_convert(node->ctype, node->operand->ctype->ptr);
}

static void emit_ternary(Node *node) {
    SAVE;
    emit_expr(node->cond);
    char *ne = make_label();
    emit_je(ne);
    if (node->then)
        emit_expr(node->then);
    if (node->els) {
        char *end = make_label();
        emit_jmp(end);
        emit_label(ne);
        emit_expr(node->els);
        emit_label(end);
    } else {
        emit_label(ne);
    }
}

#define SET_JUMP_LABELS(brk, cont)              \
    char *obreak = lbreak;                      \
    char *ocontinue = lcontinue;                \
    lbreak = brk;                               \
    lcontinue = cont
#define RESTORE_JUMP_LABELS()                   \
    lbreak = obreak;                            \
    lcontinue = ocontinue

static void emit_for(Node *node) {
    SAVE;
    if (node->forinit)
        emit_expr(node->forinit);
    char *begin = make_label();
    char *step = make_label();
    char *end = make_label();
    SET_JUMP_LABELS(end, step);
    emit_label(begin);
    if (node->forcond) {
        emit_expr(node->forcond);
        emit_je(end);
    }
    if (node->forbody)
        emit_expr(node->forbody);
    emit_label(step);
    if (node->forstep)
        emit_expr(node->forstep);
    emit_jmp(begin);
    emit_label(end);
    RESTORE_JUMP_LABELS();
}

static void emit_while(Node *node) {
    SAVE;
    char *begin = make_label();
    char *end = make_label();
    SET_JUMP_LABELS(end, begin);
    emit_label(begin);
    emit_expr(node->forcond);
    emit_je(end);
    if (node->forbody)
        emit_expr(node->forbody);
    emit_jmp(begin);
    emit_label(end);
    RESTORE_JUMP_LABELS();
}

static void emit_do(Node *node) {
    SAVE;
    char *begin = make_label();
    char *end = make_label();
    SET_JUMP_LABELS(end, begin);
    emit_label(begin);
    if (node->forbody)
        emit_expr(node->forbody);
    emit_expr(node->forcond);
    emit_je(end);
    emit_jmp(begin);
    emit_label(end);
    RESTORE_JUMP_LABELS();
}

#undef SET_JUMP_LABELS
#undef RESTORE_JUMP_LABELS

static void emit_switch(Node *node) {
    SAVE;
    char *oswitch = lswitch, *obreak = lbreak;
    emit_expr(node->switchexpr);
    lswitch = make_label();
    lbreak = make_label();
    emit_jmp(lswitch);
    if (node->switchbody)
        emit_expr(node->switchbody);
    emit_label(lswitch);
    emit_label(lbreak);
    lswitch = oswitch;
    lbreak = obreak;
}

static void emit_case(Node *node) {
    SAVE;
    if (!lswitch)
        error("stray case label");
    char *skip = make_label();
    emit_jmp(skip);
    emit_label(lswitch);
    lswitch = make_label();
    emit("cmp $%d, %%eax", node->casebeg);
    if (node->casebeg == node->caseend) {
        emit("jne %s", lswitch);
    } else {
        emit("jl %s", lswitch);
        emit("cmp $%d, %%eax", node->caseend);
        emit("jg %s", lswitch);
    }
    emit_label(skip);
}

static void emit_default(Node *node) {
    SAVE;
    if (!lswitch)
        error("stray case label");
    emit_label(lswitch);
    lswitch = make_label();
}

static void emit_goto(Node *node) {
    SAVE;
    assert(node->newlabel);
    emit_jmp(node->newlabel);
}

static void emit_return(Node *node) {
    SAVE;
    if (node->retval) {
        emit_expr(node->retval);
        maybe_booleanize_retval(node->retval->ctype);
    }
    emit_ret();
}

static void emit_break(Node *node) {
    SAVE;
    if (!lbreak)
        error("stray break statement");
    emit_jmp(lbreak);
}

static void emit_continue(Node *node) {
    SAVE;
    if (!lcontinue)
        error("stray continue statement");
    emit_jmp(lcontinue);
}

static void emit_compound_stmt(Node *node) {
    SAVE;
    for (Iter *i = list_iter(node->stmts); !iter_end(i);)
        emit_expr(iter_next(i));
}

static void emit_va_start(Node *node) {
    SAVE;
    emit_expr(node->ap);
    push("rcx");
    emit("movl $%d, (%%rax)", numgp * 8);
    emit("movl $%d, 4(%%rax)", 48 + numfp * 16);
    emit("lea %d(%%rbp), %%rcx", -REGAREA_SIZE);
    emit("mov %%rcx, 16(%%rax)");
    pop("rcx");
}

static void emit_va_arg(Node *node) {
    SAVE;
    emit_expr(node->ap);
    emit("nop");
    push("rcx");
    push("rbx");
    emit("mov 16(%%rax), %%rcx");
    if (is_flotype(node->ctype)) {
        emit("mov 4(%%rax), %%ebx");
        emit("add %%rbx, %%rcx");
        emit("add $16, %%ebx");
        emit("mov %%ebx, 4(%%rax)");
        emit("movsd (%%rcx), %%xmm0");
        if (node->ctype->type == CTYPE_FLOAT)
            emit("cvtpd2ps %%xmm0, %%xmm0");
    } else {
        emit("mov (%%rax), %%ebx");
        emit("add %%rbx, %%rcx");
        emit("add $8, %%ebx");
        emit("mov %%rbx, (%%rax)");
        emit("mov (%%rcx), %%rax");
    }
    pop("rbx");
    pop("rcx");
}

static void emit_logand(Node *node) {
    SAVE;
    char *end = make_label();
    emit_expr(node->left);
    emit("test %%rax, %%rax");
    emit("mov $0, %%rax");
    emit("je %s", end);
    emit_expr(node->right);
    emit("test %%rax, %%rax");
    emit("mov $0, %%rax");
    emit("je %s", end);
    emit("mov $1, %%rax");
    emit_label(end);
}

static void emit_logor(Node *node) {
    SAVE;
    char *end = make_label();
    emit_expr(node->left);
    emit("test %%rax, %%rax");
    emit("mov $1, %%rax");
    emit("jne %s", end);
    emit_expr(node->right);
    emit("test %%rax, %%rax");
    emit("mov $1, %%rax");
    emit("jne %s", end);
    emit("mov $0, %%rax");
    emit_label(end);
}

static void emit_lognot(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit("cmp $0, %%rax");
    emit("sete %%al");
    emit("movzb %%al, %%eax");
}

static void emit_bitand(Node *node) {
    SAVE;
    emit_expr(node->left);
    push("rax");
    emit_expr(node->right);
    pop("rcx");
    emit("and %%rcx, %%rax");
}

static void emit_bitor(Node *node) {
    SAVE;
    emit_expr(node->left);
    push("rax");
    emit_expr(node->right);
    pop("rcx");
    emit("or %%rcx, %%rax");
}

static void emit_bitnot(Node *node) {
    SAVE;
    emit_expr(node->left);
    emit("not %%rax");
}

static void emit_cast(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_load_convert(node->ctype, node->operand->ctype);
    return;
}

static void emit_comma(Node *node) {
    SAVE;
    emit_expr(node->left);
    emit_expr(node->right);
}

static void emit_assign(Node *node) {
    SAVE;
    if (node->left->ctype->type == CTYPE_STRUCT &&
        node->left->ctype->size > 8) {
        emit_copy_struct(node->left, node->right);
    } else {
        emit_expr(node->right);
        emit_load_convert(node->ctype, node->right->ctype);
        emit_store(node->left);
    }
}

static void emit_label_addr(Node *node) {
    SAVE;
    emit("mov $%s, %%rax", node->newlabel);
}

static void emit_computed_goto(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit("jmp *%%rax");
}

static void emit_expr(Node *node) {
    SAVE;
    switch (node->type) {
    case AST_LITERAL: emit_literal(node); return;
    case AST_STRING:  emit_literal_string(node); return;
    case AST_LVAR:    emit_lvar(node); return;
    case AST_GVAR:    emit_gvar(node); return;
    case AST_FUNCALL:
    case AST_FUNCPTR_CALL:
        emit_func_call(node);
        return;
    case AST_DECL:    emit_decl(node); return;
    case AST_CONV:    emit_conv(node); return;
    case AST_ADDR:    emit_addr(node->operand); return;
    case AST_DEREF:   emit_deref(node); return;
    case AST_IF:
    case AST_TERNARY:
        emit_ternary(node);
        return;
    case AST_FOR:     emit_for(node); return;
    case AST_WHILE:   emit_while(node); return;
    case AST_DO:      emit_do(node); return;
    case AST_SWITCH:  emit_switch(node); return;
    case AST_CASE:    emit_case(node); return;
    case AST_DEFAULT: emit_default(node); return;
    case AST_GOTO:    emit_goto(node); return;
    case AST_LABEL:
        if (node->newlabel)
            emit_label(node->newlabel);
        return;
    case AST_RETURN:  emit_return(node); return;
    case AST_BREAK:   emit_break(node); return;
    case AST_CONTINUE: emit_continue(node); return;
    case AST_COMPOUND_STMT: emit_compound_stmt(node); return;
    case AST_STRUCT_REF:
        emit_load_struct_ref(node->struc, node->ctype, 0);
        return;
    case AST_VA_START: emit_va_start(node); return;
    case AST_VA_ARG:   emit_va_arg(node); return;
    case OP_UMINUS:    emit_uminus(node); return;
    case OP_PRE_INC:   emit_pre_inc_dec(node, "add"); return;
    case OP_PRE_DEC:   emit_pre_inc_dec(node, "sub"); return;
    case OP_POST_INC:  emit_post_inc_dec(node, "add"); return;
    case OP_POST_DEC:  emit_post_inc_dec(node, "sub"); return;
    case '!': emit_lognot(node); return;
    case '&': emit_bitand(node); return;
    case '|': emit_bitor(node); return;
    case '~': emit_bitnot(node); return;
    case OP_LOGAND: emit_logand(node); return;
    case OP_LOGOR:  emit_logor(node); return;
    case OP_CAST:   emit_cast(node); return;
    case ',': emit_comma(node); return;
    case '=': emit_assign(node); return;
    case OP_LABEL_ADDR: emit_label_addr(node); return;
    case AST_COMPUTED_GOTO: emit_computed_goto(node); return;
    default:
        emit_binop(node);
    }
}

static void emit_zero(int size) {
    SAVE;
    for (; size >= 8; size -= 8) emit(".quad 0");
    for (; size >= 4; size -= 4) emit(".long 0");
    for (; size > 0; size--)     emit(".byte 0");
}

static void emit_padding(Node *node, int off) {
    SAVE;
    int diff = node->initoff - off;
    assert(diff >= 0);
    emit_zero(diff);
}

static void emit_data_addr(Node *operand, int depth) {
    switch (operand->type) {
    case AST_LVAR: {
        char *label = make_label();
        emit(".data %d", depth + 1);
        emit_label(label);
        emit_data_int(operand->lvarinit, operand->ctype->size, 0, depth + 1);
        emit(".data %d", depth);
        emit(".quad %s", label);
        return;
    }
    case AST_GVAR:
        emit(".quad %s", operand->varname);
        return;
    default:
        error("internal error");
    }
}

static void emit_data_charptr(char *s, int depth) {
    char *label = make_label();
    emit(".data %d", depth + 1);
    emit_label(label);
    emit(".string \"%s\"", quote_cstring(s));
    emit(".data %d", depth);
    emit(".quad %s", label);
}

static void emit_data_primtype(Ctype *ctype, Node *val) {
    switch (ctype->type) {
    case CTYPE_FLOAT: {
        float v = val->fval;
        emit(".long %d", *(int *)&v);
        break;
    }
    case CTYPE_DOUBLE:
        emit(".quad %ld", *(long *)&val->fval);
        break;
    case CTYPE_BOOL:
        emit(".byte %d", !!eval_intexpr(val));
        break;
    case CTYPE_CHAR:
        emit(".byte %d", eval_intexpr(val));
        break;
    case CTYPE_SHORT:
        emit(".short %d", eval_intexpr(val));
        break;
    case CTYPE_INT:
        emit(".long %d", eval_intexpr(val));
        break;
    case CTYPE_LONG:
    case CTYPE_LLONG:
    case CTYPE_PTR:
        if (val->type == AST_GVAR)
            emit(".quad %s", val->varname);
        else
            emit(".quad %d", eval_intexpr(val));
        break;
    default:
        error("don't know how to handle\n  <%s>\n  <%s>", c2s(ctype), a2s(val));
    }
}

static void emit_data_int(List *inits, int size, int off, int depth) {
    SAVE;
    Iter *iter = list_iter(inits);
    while (!iter_end(iter) && 0 < size) {
        Node *node = iter_next(iter);
        Node *v = node->initval;
        emit_padding(node, off);
        if (node->totype->bitsize > 0) {
            assert(node->totype->bitoff == 0);
            long data = eval_intexpr(v);
            Ctype *totype = node->totype;
            while (!iter_end(iter)) {
                node = iter_next(iter);
                if (node->totype->bitsize <= 0) {
                    break;
                }
                v = node->initval;
                totype = node->totype;
                data |= ((((long)1 << totype->bitsize) - 1) & eval_intexpr(v)) << totype->bitoff;
            }
            emit_data_primtype(totype, &(Node){ AST_LITERAL, totype, .ival = data });
            off += totype->size;
            size -= totype->size;
            if (iter_end(iter))
                break;
        } else {
            off += node->totype->size;
            size -= node->totype->size;
        }
        if (v->type == AST_ADDR) {
            emit_data_addr(v->operand, depth);
            continue;
        }
        if (v->type == AST_LVAR && v->lvarinit) {
            emit_data_int(v->lvarinit, v->ctype->size, 0, depth);
            continue;
        }
        bool is_char_ptr = (v->ctype->type == CTYPE_ARRAY && v->ctype->ptr->type == CTYPE_CHAR);
        if (is_char_ptr) {
            emit_data_charptr(v->sval, depth);
            continue;
        }
        emit_data_primtype(node->totype, node->initval);
    }
    emit_zero(size);
}

static void emit_data(Node *v, int off, int depth) {
    SAVE;
    emit(".data %d", depth);
    if (!v->declvar->ctype->isstatic)
        emit_noindent(".global %s", v->declvar->varname);
    emit_noindent("%s:", v->declvar->varname);
    emit_data_int(v->declinit, v->declvar->ctype->size, off, depth);
}

static void emit_bss(Node *v) {
    SAVE;
    emit(".data");
    if (!v->declvar->ctype->isstatic)
        emit(".global %s", v->declvar->varname);
    emit(".lcomm %s, %d", v->declvar->varname, v->declvar->ctype->size);
}

static void emit_global_var(Node *v) {
    SAVE;
    if (v->declinit)
        emit_data(v, 0, 0);
    else
        emit_bss(v);
}

static int align(int n, int m) {
    int rem = n % m;
    return (rem == 0) ? n : n - rem + m;
}

static int emit_regsave_area(void) {
    int pos = -REGAREA_SIZE;
    emit("mov %%rdi, %d(%%rsp)", pos);
    emit("mov %%rsi, %d(%%rsp)", (pos += 8));
    emit("mov %%rdx, %d(%%rsp)", (pos += 8));
    emit("mov %%rcx, %d(%%rsp)", (pos += 8));
    emit("mov %%r8, %d(%%rsp)", (pos += 8));
    emit("mov %%r9, %d(%%rsp)", pos + 8);
    char *end = make_label();
    for (int i = 0; i < 16; i++) {
        emit("test %%al, %%al");
        emit("jz %s", end);
        emit("movsd %%xmm%d, %d(%%rsp)", i, (pos += 16));
        emit("sub $1, %%al");
    }
    emit_label(end);
    emit("sub $%d, %%rsp", REGAREA_SIZE);
    return REGAREA_SIZE;
}

static void push_func_params(List *params, int off) {
    int ireg = 0;
    int xreg = 0;
    int arg = 2;
    for (Iter *i = list_iter(params); !iter_end(i);) {
        Node *v = iter_next(i);
        if (is_flotype(v->ctype)) {
            if (xreg >= 8) {
                emit("mov %d(%%rbp), %%rax", arg++ * 8);
                push("rax");
            } else {
                push_xmm(xreg++);
            }
        } else {
            if (ireg >= 6) {
                if (v->ctype->type == CTYPE_BOOL) {
                    emit("mov %d(%%rbp), %%al", arg++ * 8);
                    emit("movzb %%al, %%eax");
                } else {
                    emit("mov %d(%%rbp), %%rax", arg++ * 8);
                }
                push("rax");
            } else {
                if (v->ctype->type == CTYPE_BOOL)
                    emit("movzb %%%s, %%%s", SREGS[ireg], MREGS[ireg]);
                push(REGS[ireg++]);
            }
        }
        off -= 8;
        v->loff = off;
    }
}

static void emit_func_prologue(Node *func) {
    SAVE;
    emit(".text");
    if (!func->ctype->isstatic)
        emit_noindent(".global %s", func->fname);
    emit_noindent("%s:", func->fname);
    emit("nop");
    push("rbp");
    emit("mov %%rsp, %%rbp");
    int off = 0;
    if (func->ctype->hasva) {
        set_reg_nums(func->params);
        off -= emit_regsave_area();
    }
    push_func_params(func->params, off);
    off -= list_len(func->params) * 8;

    int localarea = 0;
    for (Iter *i = list_iter(func->localvars); !iter_end(i);) {
        Node *v = iter_next(i);
        int size = align(v->ctype->size, 8);
        assert(size % 8 == 0);
        off -= size;
        v->loff = off;
        localarea += size;
    }
    if (localarea) {
        emit("sub $%d, %%rsp", localarea);
        stackpos += localarea;
    }
}

void emit_toplevel(Node *v) {
    stackpos = 8;
    if (v->type == AST_FUNC) {
        emit_func_prologue(v);
        emit_expr(v->body);
        emit_ret();
    } else if (v->type == AST_DECL) {
        emit_global_var(v);
    } else {
        error("internal error");
    }
}
