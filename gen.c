// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "8cc.h"

bool dumpstack = false;
bool dumpsource = true;

static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char *SREGS[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *MREGS[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
static int TAB = 8;
static Vector *functions = &EMPTY_VECTOR;
static char *lbreak;
static char *lcontinue;
static char *lswitch;
static int stackpos;
static int numgp;
static int numfp;
static FILE *outputfp;
static Map *source_files = &EMPTY_MAP;
static Map *source_lines = &EMPTY_MAP;
static char *last_loc = "";

static void emit_addr(Node *node);
static void emit_expr(Node *node);
static void emit_decl_init(Vector *inits, int off);
static void do_emit_data(Vector *inits, int size, int off, int depth);
static void emit_data(Node *v, int off, int depth);

#define REGAREA_SIZE 304

#define emit(...)        emitf(__LINE__, "\t" __VA_ARGS__)
#define emit_noindent(...)  emitf(__LINE__, __VA_ARGS__)

#ifdef __GNUC__
#define SAVE                                                            \
    int save_hook __attribute__((unused, cleanup(pop_function)));       \
    if (dumpstack)                                                      \
        vec_push(functions, (void *)__func__);

static void pop_function(void *ignore) {
    if (dumpstack)
        vec_pop(functions);
}
#else
#define SAVE
#endif

static char *get_caller_list(void) {
    Buffer *b = make_buffer();
    for (int i = 0; i < vec_len(functions); i++) {
        if (i > 0)
            buf_printf(b, " -> ");
        buf_printf(b, "%s", vec_get(functions, i));
    }
    buf_write(b, '\0');
    return buf_body(b);
}

void set_output_file(FILE *fp) {
    outputfp = fp;
}

void close_output_file(void) {
    fclose(outputfp);
}

static void emitf(int line, char *fmt, ...) {
    // Replace "#" with "%%" so that vfprintf prints out "#" as "%".
    char buf[256];
    int i = 0;
    for (char *p = fmt; *p; p++) {
        assert(i < sizeof(buf) - 3);
        if (*p == '#') {
            buf[i++] = '%';
            buf[i++] = '%';
        } else {
            buf[i++] = *p;
        }
    }
    buf[i] = '\0';

    va_list args;
    va_start(args, fmt);
    int col = vfprintf(outputfp, buf, args);
    va_end(args);

    if (dumpstack) {
        for (char *p = fmt; *p; p++)
            if (*p == '\t')
                col += TAB - 1;
        int space = (28 - col) > 0 ? (30 - col) : 2;
        fprintf(outputfp, "%*c %s:%d", space, '#', get_caller_list(), line);
    }
    fprintf(outputfp, "\n");
}

static void emit_nostack(char *fmt, ...) {
    fprintf(outputfp, "\t");
    va_list args;
    va_start(args, fmt);
    vfprintf(outputfp, fmt, args);
    va_end(args);
    fprintf(outputfp, "\n");
}

static char *get_int_reg(Type *ty, char r) {
    assert(r == 'a' || r == 'c');
    switch (ty->size) {
    case 1: return (r == 'a') ? "al" : "cl";
    case 2: return (r == 'a') ? "ax" : "cx";
    case 4: return (r == 'a') ? "eax" : "ecx";
    case 8: return (r == 'a') ? "rax" : "rcx";
    default:
        error("Unknown data size: %s: %d", c2s(ty), ty->size);
    }
}

static char *get_load_inst(Type *ty) {
    switch (ty->size) {
    case 1: return "movsbq";
    case 2: return "movswq";
    case 4: return "movslq";
    case 8: return "mov";
    default:
        error("Unknown data size: %s: %d", c2s(ty), ty->size);
    }
}

static int align(int n, int m) {
    int rem = n % m;
    return (rem == 0) ? n : n - rem + m;
}

static void push_xmm(int reg) {
    SAVE;
    emit("sub $8, #rsp");
    emit("movsd #xmm%d, (#rsp)", reg);
    stackpos += 8;
}

static void pop_xmm(int reg) {
    SAVE;
    emit("movsd (#rsp), #xmm%d", reg);
    emit("add $8, #rsp");
    stackpos -= 8;
    assert(stackpos >= 0);
}

static void push(char *reg) {
    SAVE;
    emit("push #%s", reg);
    stackpos += 8;
}

static void pop(char *reg) {
    SAVE;
    emit("pop #%s", reg);
    stackpos -= 8;
    assert(stackpos >= 0);
}

static int push_struct(int size) {
    SAVE;
    int aligned = align(size, 8);
    emit("sub $%d, #rsp", aligned);
    emit("mov #rcx, -8(#rsp)");
    emit("mov #r11, -16(#rsp)");
    emit("mov #rax, #rcx");
    int i = 0;
    for (; i < size; i += 8) {
        emit("movq %d(#rcx), #r11", i);
        emit("mov #r11, %d(#rsp)", i);
    }
    for (; i < size; i += 4) {
        emit("movl %d(#rcx), #r11", i);
        emit("movl #r11d, %d(#rsp)", i);
    }
    for (; i < size; i++) {
        emit("movb %d(#rcx), #r11", i);
        emit("movb #r11b, %d(#rsp)", i);
    }
    emit("mov -8(#rsp), #rcx");
    emit("mov -16(#rsp), #r11");
    stackpos += aligned;
    return aligned;
}

static void maybe_emit_bitshift_load(Type *ty) {
    SAVE;
    if (ty->bitsize <= 0)
        return;
    emit("shr $%d, #rax", ty->bitoff);
    push("rcx");
    emit("mov $0x%lx, #rcx", (1 << (long)ty->bitsize) - 1);
    emit("and #rcx, #rax");
    pop("rcx");
}

static void maybe_emit_bitshift_save(Type *ty, char *addr) {
    SAVE;
    if (ty->bitsize <= 0)
        return;
    push("rcx");
    push("rdi");
    emit("mov $0x%lx, #rdi", (1 << (long)ty->bitsize) - 1);
    emit("and #rdi, #rax");
    emit("shl $%d, #rax", ty->bitoff);
    emit("mov %s, #%s", addr, get_int_reg(ty, 'c'));
    emit("mov $0x%lx, #rdi", ~(((1 << (long)ty->bitsize) - 1) << ty->bitoff));
    emit("and #rdi, #rcx");
    emit("or #rcx, #rax");
    pop("rdi");
    pop("rcx");
}

static void emit_gload(Type *ty, char *label, int off) {
    SAVE;
    if (ty->kind == KIND_ARRAY) {
        if (off)
            emit("lea %s+%d(#rip), #rax", label, off);
        else
            emit("lea %s(#rip), #rax", label);
        return;
    }
    char *inst = get_load_inst(ty);
    emit("%s %s+%d(#rip), #rax", inst, label, off);
    maybe_emit_bitshift_load(ty);
}

static void emit_toint(Type *ty) {
    SAVE;
    if (ty->kind == KIND_FLOAT)
        emit("cvttss2si #xmm0, #eax");
    else if (ty->kind == KIND_DOUBLE)
        emit("cvttsd2si #xmm0, #eax");
}

static void emit_lload(Type *ty, char *base, int off) {
    SAVE;
    if (ty->kind == KIND_ARRAY) {
        emit("lea %d(#%s), #rax", off, base);
    } else if (ty->kind == KIND_FLOAT) {
        emit("movss %d(#%s), #xmm0", off, base);
    } else if (ty->kind == KIND_DOUBLE || ty->kind == KIND_LDOUBLE) {
        emit("movsd %d(#%s), #xmm0", off, base);
    } else {
        char *inst = get_load_inst(ty);
        emit("%s %d(#%s), #rax", inst, off, base);
        maybe_emit_bitshift_load(ty);
    }
}

static void maybe_convert_bool(Type *ty) {
    if (ty->kind == KIND_BOOL) {
        emit("test #rax, #rax");
        emit("setne #al");
    }
}

static void emit_gsave(char *varname, Type *ty, int off) {
    SAVE;
    assert(ty->kind != KIND_ARRAY);
    maybe_convert_bool(ty);
    char *reg = get_int_reg(ty, 'a');
    char *addr = format("%s+%d(%%rip)", varname, off);
    maybe_emit_bitshift_save(ty, addr);
    emit("mov #%s, %s", reg, addr);
}

static void emit_lsave(Type *ty, int off) {
    SAVE;
    if (ty->kind == KIND_FLOAT) {
        emit("movss #xmm0, %d(#rbp)", off);
    } else if (ty->kind == KIND_DOUBLE) {
        emit("movsd #xmm0, %d(#rbp)", off);
    } else {
        maybe_convert_bool(ty);
        char *reg = get_int_reg(ty, 'a');
        char *addr = format("%d(%%rbp)", off);
        maybe_emit_bitshift_save(ty, addr);
        emit("mov #%s, %s", reg, addr);
    }
}

static void do_emit_assign_deref(Type *ty, int off) {
    SAVE;
    emit("mov (#rsp), #rcx");
    char *reg = get_int_reg(ty, 'c');
    if (off)
        emit("mov #%s, %d(#rax)", reg, off);
    else
        emit("mov #%s, (#rax)", reg);
    pop("rax");
}

static void emit_assign_deref(Node *var) {
    SAVE;
    push("rax");
    emit_expr(var->operand);
    do_emit_assign_deref(var->operand->ty->ptr, 0);
}

static void emit_pointer_arith(char kind, Node *left, Node *right) {
    SAVE;
    emit_expr(left);
    push("rcx");
    push("rax");
    emit_expr(right);
    int size = left->ty->ptr->size;
    if (size > 1)
        emit("imul $%d, #rax", size);
    emit("mov #rax, #rcx");
    pop("rax");
    switch (kind) {
    case '+': emit("add #rcx, #rax"); break;
    case '-': emit("sub #rcx, #rax"); break;
    default: error("invalid operator '%d'", kind);
    }
    pop("rcx");
}

static void emit_zero_filler(int start, int end) {
    SAVE;
    for (; start <= end - 4; start += 4)
        emit("movl $0, %d(#rbp)", start);
    for (; start < end; start++)
        emit("movb $0, %d(#rbp)", start);
}

static void ensure_lvar_init(Node *node) {
    SAVE;
    assert(node->kind == AST_LVAR);
    if (node->lvarinit) {
        emit_zero_filler(node->loff, node->loff + node->ty->size);
        emit_decl_init(node->lvarinit, node->loff);
    }
    node->lvarinit = NULL;
}

static void emit_assign_struct_ref(Node *struc, Type *field, int off) {
    SAVE;
    switch (struc->kind) {
    case AST_LVAR:
        ensure_lvar_init(struc);
        emit_lsave(field, struc->loff + field->offset + off);
        break;
    case AST_GVAR:
        emit_gsave(struc->glabel, field, field->offset + off);
        break;
    case AST_STRUCT_REF:
        emit_assign_struct_ref(struc->struc, field, off + struc->ty->offset);
        break;
    case AST_DEREF:
        push("rax");
        emit_expr(struc->operand);
        do_emit_assign_deref(field, field->offset + off);
        break;
    default:
        error("internal error: %s", a2s(struc));
    }
}

static void emit_load_struct_ref(Node *struc, Type *field, int off) {
    SAVE;
    switch (struc->kind) {
    case AST_LVAR:
        ensure_lvar_init(struc);
        emit_lload(field, "rbp", struc->loff + field->offset + off);
        break;
    case AST_GVAR:
        emit_gload(field, struc->glabel, field->offset + off);
        break;
    case AST_STRUCT_REF:
        emit_load_struct_ref(struc->struc, field, struc->ty->offset + off);
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
    switch (var->kind) {
    case AST_DEREF: emit_assign_deref(var); break;
    case AST_STRUCT_REF: emit_assign_struct_ref(var->struc, var->ty, 0); break;
    case AST_LVAR:
        ensure_lvar_init(var);
        emit_lsave(var->ty, var->loff);
        break;
    case AST_GVAR: emit_gsave(var->glabel, var->ty, 0); break;
    default: error("internal error");
    }
}

static void emit_to_bool(Type *ty) {
    SAVE;
    if (is_flotype(ty)) {
        push_xmm(1);
        emit("xorpd #xmm1, #xmm1");
        emit("%s #xmm1, #xmm0", (ty->kind == KIND_FLOAT) ? "ucomiss" : "ucomisd");
        emit("setne #al");
        pop_xmm(1);
    } else {
        emit("cmp $0, #rax");
        emit("setne #al");
    }
    emit("movzb #al, #eax");
}

static void emit_comp(char *inst, Node *node) {
    SAVE;
    if (is_flotype(node->left->ty)) {
        emit_expr(node->left);
        push_xmm(0);
        emit_expr(node->right);
        pop_xmm(1);
        if (node->left->ty->kind == KIND_FLOAT)
            emit("ucomiss #xmm0, #xmm1");
        else
            emit("ucomisd #xmm0, #xmm1");
    } else {
        emit_expr(node->left);
        push("rax");
        emit_expr(node->right);
        pop("rcx");
        int kind = node->left->ty->kind;
        if (kind == KIND_LONG || kind == KIND_LLONG)
          emit("cmp #rax, #rcx");
        else
          emit("cmp #eax, #ecx");
    }
    emit("%s #al", inst);
    emit("movzb #al, #eax");
}

static void emit_binop_int_arith(Node *node) {
    SAVE;
    char *op = NULL;
    switch (node->kind) {
    case '+': op = "add"; break;
    case '-': op = "sub"; break;
    case '*': op = "imul"; break;
    case '^': op = "xor"; break;
    case OP_SAL: op = "sal"; break;
    case OP_SAR: op = "sar"; break;
    case OP_SHR: op = "shr"; break;
    case '/': case '%': break;
    default: error("invalid operator '%d'", node->kind);
    }
    emit_expr(node->left);
    push("rax");
    emit_expr(node->right);
    emit("mov #rax, #rcx");
    pop("rax");
    if (node->kind == '/' || node->kind == '%') {
        if (node->ty->usig) {
          emit("xor #edx, #edx");
          emit("div #rcx");
        } else {
          emit("cqto");
          emit("idiv #rcx");
        }
        if (node->kind == '%')
            emit("mov #edx, #eax");
    } else if (node->kind == OP_SAL || node->kind == OP_SAR || node->kind == OP_SHR) {
        emit("%s #cl, #%s", op, get_int_reg(node->left->ty, 'a'));
    } else {
        emit("%s #rcx, #rax", op);
    }
}

static void emit_binop_float_arith(Node *node) {
    SAVE;
    char *op;
    bool isdouble = (node->ty->kind == KIND_DOUBLE);
    switch (node->kind) {
    case '+': op = (isdouble ? "addsd" : "addss"); break;
    case '-': op = (isdouble ? "subsd" : "subss"); break;
    case '*': op = (isdouble ? "mulsd" : "mulss"); break;
    case '/': op = (isdouble ? "divsd" : "divss"); break;
    default: error("invalid operator '%d'", node->kind);
    }
    emit_expr(node->left);
    push_xmm(0);
    emit_expr(node->right);
    emit("%s #xmm0, #xmm1", (isdouble ? "movsd" : "movss"));
    pop_xmm(0);
    emit("%s #xmm1, #xmm0", op);
}

static void emit_load_convert(Type *to, Type *from) {
    SAVE;
    if (is_inttype(from) && to->kind == KIND_FLOAT)
        emit("cvtsi2ss #eax, #xmm0");
    else if (is_inttype(from) && to->kind == KIND_DOUBLE)
        emit("cvtsi2sd #eax, #xmm0");
    else if (from->kind == KIND_FLOAT && to->kind == KIND_DOUBLE)
        emit("cvtps2pd #xmm0, #xmm0");
    else if ((from->kind == KIND_DOUBLE || from->kind == KIND_LDOUBLE) && to->kind == KIND_FLOAT)
        emit("cvtpd2ps #xmm0, #xmm0");
    else if (to->kind == KIND_BOOL)
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
    if (node->ty->kind == KIND_PTR) {
        emit_pointer_arith(node->kind, node->left, node->right);
        return;
    }
    switch (node->kind) {
    case '<': emit_comp("setl", node); return;
    case '>': emit_comp("setg", node); return;
    case OP_EQ: emit_comp("sete", node); return;
    case OP_GE: emit_comp("setge", node); return;
    case OP_LE: emit_comp("setle", node); return;
    case OP_NE: emit_comp("setne", node); return;
    }
    if (is_inttype(node->ty))
        emit_binop_int_arith(node);
    else if (is_flotype(node->ty))
        emit_binop_float_arith(node);
    else
        error("internal error: %s", a2s(node));
}

static void emit_save_literal(Node *node, Type *totype, int off) {
    switch (totype->kind) {
    case KIND_BOOL:  emit("movb $%d, %d(#rbp)", !!node->ival, off); break;
    case KIND_CHAR:  emit("movb $%d, %d(#rbp)", node->ival, off); break;
    case KIND_SHORT: emit("movw $%d, %d(#rbp)", node->ival, off); break;
    case KIND_INT:   emit("movl $%d, %d(#rbp)", node->ival, off); break;
    case KIND_LONG:
    case KIND_LLONG:
    case KIND_PTR: {
        unsigned long ival = node->ival;
        emit("movl $%lu, %d(#rbp)", ival & ((1L << 32) - 1), off);
        emit("movl $%lu, %d(#rbp)", ival >> 32, off + 4);
        break;
    }
    case KIND_FLOAT: {
        float fval = node->fval;
        int *p = (int *)&fval;
        emit("movl $%u, %d(#rbp)", *p, off);
        break;
    }
    case KIND_DOUBLE:
    case KIND_LDOUBLE: {
        unsigned long *p = (unsigned long *)&node->fval;
        emit("movl $%lu, %d(#rbp)", *p & ((1L << 32) - 1), off);
        emit("movl $%lu, %d(#rbp)", *p >> 32, off + 4);
        break;
    }
    default:
        error("internal error: <%s> <%s> <%d>", a2s(node), c2s(totype), off);
    }
}

static void emit_addr(Node *node) {
    switch (node->kind) {
    case AST_LVAR:
        ensure_lvar_init(node);
        emit("lea %d(#rbp), #rax", node->loff);
        break;
    case AST_GVAR:
        emit("lea %s(#rip), #rax", node->glabel);
        break;
    case AST_DEREF:
        emit_expr(node->operand);
        break;
    case AST_STRUCT_REF:
        emit_addr(node->struc);
        emit("add $%d, #rax", node->ty->offset);
        break;
    default:
        error("internal error: %s", a2s(node));
    }
}

static void emit_copy_struct(Node *left, Node *right) {
    push("rcx");
    push("r11");
    emit_addr(right);
    emit("mov #rax, #rcx");
    emit_addr(left);
    int i = 0;
    for (; i < left->ty->size; i += 8) {
        emit("movq %d(#rcx), #r11", i);
        emit("movq #r11, %d(#rax)", i);
    }
    for (; i < left->ty->size; i += 4) {
        emit("movl %d(#rcx), #r11", i);
        emit("movl #r11, %d(#rax)", i);
    }
    for (; i < left->ty->size; i++) {
        emit("movb %d(#rcx), #r11", i);
        emit("movb #r11, %d(#rax)", i);
    }
    pop("r11");
    pop("rcx");
}

static void emit_decl_init(Vector *inits, int off) {
    for (int i = 0; i < vec_len(inits); i++) {
        Node *node = vec_get(inits, i);
        assert(node->kind == AST_INIT);
        if (node->initval->kind == AST_LITERAL &&
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
    if (is_flotype(node->ty)) {
        push_xmm(1);
        emit("xorpd #xmm1, #xmm1");
        emit("%s #xmm1, #xmm0", (node->ty->kind == KIND_DOUBLE ? "subsd" : "subss"));
        pop_xmm(1);
    } else {
        emit("neg #rax");
    }
}

static void emit_pre_inc_dec(Node *node, char *op) {
    emit_expr(node->operand);
    emit("%s $1, #rax", op);
    emit_store(node->operand);
}

static void emit_post_inc_dec(Node *node, char *op) {
    SAVE;
    emit_expr(node->operand);
    push("rax");
    emit("%s $1, #rax", op);
    emit_store(node->operand);
    pop("rax");
}

static void set_reg_nums(Vector *args) {
    numgp = numfp = 0;
    for (int i = 0; i < vec_len(args); i++) {
        Node *arg = vec_get(args, i);
        if (is_flotype(arg->ty))
            numfp++;
        else
            numgp++;
    }
}

static void emit_je(char *label) {
    emit("test #rax, #rax");
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
    switch (node->ty->kind) {
    case KIND_BOOL:
    case KIND_CHAR:
        emit("mov $%d, #rax", node->ival);
        break;
    case KIND_INT:
        emit("mov $%d, #rax", node->ival);
        break;
    case KIND_LONG:
    case KIND_LLONG: {
        emit("mov $%lu, #rax", node->ival);
        break;
    }
    case KIND_FLOAT: {
        if (!node->flabel) {
            node->flabel = make_label();
            float fval = node->fval;
            int *p = (int *)&fval;
            emit_noindent(".data");
            emit_label(node->flabel);
            emit(".long %d", *p);
            emit_noindent(".text");
        }
        emit("movss %s(#rip), #xmm0", node->flabel);
        break;
    }
    case KIND_DOUBLE:
    case KIND_LDOUBLE: {
        if (!node->flabel) {
            node->flabel = make_label();
            unsigned long *fval = (unsigned long *)&node->fval;
            emit_noindent(".data");
            emit_label(node->flabel);
            emit(".quad %lu", *fval);
            emit_noindent(".text");
        }
        emit("movsd %s(#rip), #xmm0", node->flabel);
        break;
    }
    default:
        error("internal error");
    }
}

static char **split(char *buf) {
    char *p = buf;
    int len = 1;
    while (*p) {
        if (p[0] == '\r' && p[1] == '\n') {
            len++;
            p += 2;
            continue;
        }
        if (p[0] == '\r' || p[0] == '\n')
            len++;
        p++;
    }
    p = buf;
    char **r = malloc(sizeof(char *) * len);
    r[0] = buf;
    int i = 1;
    while (*p) {
        if (p[0] == '\r' && p[1] == '\n') {
            p[0] = '\0';
            p += 2;
            r[i++] = p;
            continue;
        }
        if (p[0] == '\r' || p[0] == '\n') {
            p[0] = '\0';
            r[i++] = p + 1;
        }
        p++;
    }
    return r;
}

static char **read_source_file(char *file) {
    FILE *fp = fopen(file, "r");
    if (!fp)
        return NULL;
    struct stat st;
    fstat(fileno(fp), &st);
    char *buf = malloc(st.st_size + 1);
    if (fread(buf, 1, st.st_size, fp) != st.st_size)
        return NULL;
    fclose(fp);
    buf[st.st_size] = '\0';
    return split(buf);
}

static void maybe_print_source_line(char *file, int line) {
    if (!dumpsource)
        return;
    char **lines = map_get(source_lines, file);
    if (!lines) {
        lines = read_source_file(file);
        if (!lines)
            return;
        map_put(source_lines, file, lines);
    }
    int len = 0;
    for (char **p = lines; *p; p++)
        len++;
    emit_nostack("# %s", lines[line - 1]);
}

static void maybe_print_source_loc(Node *node) {
    if (!node->sourceLoc)
        return;
    char *file = node->sourceLoc->file;
    long fileno = (long)map_get(source_files, file);
    if (!fileno) {
        fileno = map_size(source_files) + 1;
        map_put(source_files, file, (void *)fileno);
        emit(".file %ld \"%s\"", fileno, quote_cstring(file));
    }
    char *loc = format(".loc %ld %d 0", fileno, node->sourceLoc->line);
    if (strcmp(loc, last_loc)) {
        emit("%s", loc);
        maybe_print_source_line(file, node->sourceLoc->line);
    }
    last_loc = loc;
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
    emit("lea %s(#rip), #rax", node->slabel);
}

static void emit_lvar(Node *node) {
    SAVE;
    ensure_lvar_init(node);
    emit_lload(node->ty, "rbp", node->loff);
}

static void emit_gvar(Node *node) {
    SAVE;
    emit_gload(node->ty, node->glabel, 0);
}

static bool maybe_emit_builtin(Node *node) {
    SAVE;
    if (strcmp("__builtin_return_address", node->fname))
        return false;
    push("r11");
    assert(vec_len(node->args) == 1);
    emit_expr(vec_head(node->args));
    char *loop = make_label();
    char *end = make_label();
    emit("mov #rbp, #r11");
    emit_label(loop);
    emit("test #rax, #rax");
    emit("jz %s", end);
    emit("mov (#r11), #r11");
    emit("sub $1, #rax");
    emit_jmp(loop);
    emit_label(end);
    emit("mov 8(#r11), #rax");
    pop("r11");
    return true;
}

static void classify_args(Vector *ints, Vector *floats, Vector *rest, Vector *args) {
    SAVE;
    int ireg = 0, xreg = 0;
    int imax = 6, xmax = 8;
    for (int i = 0; i < vec_len(args); i++) {
        Node *v = vec_get(args, i);
        if (v->ty->kind == KIND_STRUCT)
            vec_push(rest, v);
        else if (is_flotype(v->ty))
            vec_push((xreg++ < xmax) ? floats : rest, v);
        else
            vec_push((ireg++ < imax) ? ints : rest, v);
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

static int emit_args(Vector *vals) {
    SAVE;
    int r = 0;
    for (int i = 0; i < vec_len(vals); i++) {
        Node *v = vec_get(vals, i);
        if (v->ty->kind == KIND_STRUCT) {
            emit_addr(v);
            r += push_struct(v->ty->size);
        } else if (is_flotype(v->ty)) {
            emit_expr(v);
            push_xmm(0);
            r += 8;
        } else {
            emit_expr(v);
            push("rax");
            r += 8;
        }
    }
    return r;
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

static void maybe_booleanize_retval(Type *ty) {
    if (ty->kind == KIND_BOOL) {
        emit("movzx #al, #rax");
    }
}

static void emit_func_call(Node *node) {
    SAVE;
    int opos = stackpos;
    bool isptr = (node->kind == AST_FUNCPTR_CALL);
    Type *ftype = isptr ? node->fptr->ty->ptr : node->ftype;

    Vector *ints = make_vector();
    Vector *floats = make_vector();
    Vector *rest = make_vector();
    classify_args(ints, floats, rest, node->args);
    save_arg_regs(vec_len(ints), vec_len(floats));

    bool padding = stackpos % 16;
    if (padding) {
        emit("sub $8, #rsp");
        stackpos += 8;
    }

    int restsize = emit_args(vec_reverse(rest));
    if (isptr) {
        emit_expr(node->fptr);
        push("rax");
    }
    emit_args(ints);
    emit_args(floats);
    pop_float_args(vec_len(floats));
    pop_int_args(vec_len(ints));

    if (isptr) pop("r11");
    if (ftype->hasva)
        emit("mov $%d, #eax", vec_len(floats));

    if (isptr)
        emit("call *#r11");
    else
        emit("call %s", node->fname);
    maybe_booleanize_retval(node->ty);
    if (restsize > 0) {
        emit("add $%d, #rsp", restsize);
        stackpos -= restsize;
    }
    if (padding) {
        emit("add $8, #rsp");
        stackpos -= 8;
    }
    restore_arg_regs(vec_len(ints), vec_len(floats));
    assert(opos == stackpos);
}

static void emit_decl(Node *node) {
    SAVE;
    if (!node->declinit)
        return;
    emit_zero_filler(node->declvar->loff,
                     node->declvar->loff + node->declvar->ty->size);
    emit_decl_init(node->declinit, node->declvar->loff);
}

static void emit_conv(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_load_convert(node->ty, node->operand->ty);
}

static void emit_deref(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_lload(node->operand->ty->ptr, "rax", 0);
    emit_load_convert(node->ty, node->operand->ty->ptr);
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
    emit("cmp $%d, #eax", node->casebeg);
    if (node->casebeg == node->caseend) {
        emit("jne %s", lswitch);
    } else {
        emit("jl %s", lswitch);
        emit("cmp $%d, #eax", node->caseend);
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
        maybe_booleanize_retval(node->retval->ty);
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
    for (int i = 0; i < vec_len(node->stmts); i++)
        emit_expr(vec_get(node->stmts, i));
}

static void emit_va_start(Node *node) {
    SAVE;
    emit_expr(node->ap);
    push("rcx");
    emit("movl $%d, (#rax)", numgp * 8);
    emit("movl $%d, 4(#rax)", 48 + numfp * 16);
    emit("lea %d(#rbp), #rcx", -REGAREA_SIZE);
    emit("mov #rcx, 16(#rax)");
    pop("rcx");
}

static void emit_va_arg(Node *node) {
    SAVE;
    emit_expr(node->ap);
    emit("nop");
    push("rcx");
    push("rbx");
    emit("mov 16(#rax), #rcx");
    if (is_flotype(node->ty)) {
        emit("mov 4(#rax), #ebx");
        emit("add #rbx, #rcx");
        emit("add $16, #ebx");
        emit("mov #ebx, 4(#rax)");
        emit("movsd (#rcx), #xmm0");
        if (node->ty->kind == KIND_FLOAT)
            emit("cvtpd2ps #xmm0, #xmm0");
    } else {
        emit("mov (#rax), #ebx");
        emit("add #rbx, #rcx");
        emit("add $8, #ebx");
        emit("mov #rbx, (#rax)");
        emit("mov (#rcx), #rax");
    }
    pop("rbx");
    pop("rcx");
}

static void emit_logand(Node *node) {
    SAVE;
    char *end = make_label();
    emit_expr(node->left);
    emit("test #rax, #rax");
    emit("mov $0, #rax");
    emit("je %s", end);
    emit_expr(node->right);
    emit("test #rax, #rax");
    emit("mov $0, #rax");
    emit("je %s", end);
    emit("mov $1, #rax");
    emit_label(end);
}

static void emit_logor(Node *node) {
    SAVE;
    char *end = make_label();
    emit_expr(node->left);
    emit("test #rax, #rax");
    emit("mov $1, #rax");
    emit("jne %s", end);
    emit_expr(node->right);
    emit("test #rax, #rax");
    emit("mov $1, #rax");
    emit("jne %s", end);
    emit("mov $0, #rax");
    emit_label(end);
}

static void emit_lognot(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit("cmp $0, #rax");
    emit("sete #al");
    emit("movzb #al, #eax");
}

static void emit_bitand(Node *node) {
    SAVE;
    emit_expr(node->left);
    push("rax");
    emit_expr(node->right);
    pop("rcx");
    emit("and #rcx, #rax");
}

static void emit_bitor(Node *node) {
    SAVE;
    emit_expr(node->left);
    push("rax");
    emit_expr(node->right);
    pop("rcx");
    emit("or #rcx, #rax");
}

static void emit_bitnot(Node *node) {
    SAVE;
    emit_expr(node->left);
    emit("not #rax");
}

static void emit_cast(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_load_convert(node->ty, node->operand->ty);
    return;
}

static void emit_comma(Node *node) {
    SAVE;
    emit_expr(node->left);
    emit_expr(node->right);
}

static void emit_assign(Node *node) {
    SAVE;
    if (node->left->ty->kind == KIND_STRUCT &&
        node->left->ty->size > 8) {
        emit_copy_struct(node->left, node->right);
    } else {
        emit_expr(node->right);
        emit_load_convert(node->ty, node->right->ty);
        emit_store(node->left);
    }
}

static void emit_label_addr(Node *node) {
    SAVE;
    emit("mov $%s, #rax", node->newlabel);
}

static void emit_computed_goto(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit("jmp *#rax");
}

static void emit_expr(Node *node) {
    SAVE;
    maybe_print_source_loc(node);
    switch (node->kind) {
    case AST_LITERAL: emit_literal(node); return;
    case AST_STRING:  emit_literal_string(node); return;
    case AST_LVAR:    emit_lvar(node); return;
    case AST_GVAR:    emit_gvar(node); return;
    case AST_FUNCDESG: return;
    case AST_FUNCALL:
        if (maybe_emit_builtin(node))
            return;
        // fall through
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
        emit_load_struct_ref(node->struc, node->ty, 0);
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
    switch (operand->kind) {
    case AST_LVAR: {
        char *label = make_label();
        emit(".data %d", depth + 1);
        emit_label(label);
        do_emit_data(operand->lvarinit, operand->ty->size, 0, depth + 1);
        emit(".data %d", depth);
        emit(".quad %s", label);
        return;
    }
    case AST_GVAR:
        emit(".quad %s", operand->glabel);
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

static void emit_data_primtype(Type *ty, Node *val, int depth) {
    switch (ty->kind) {
    case KIND_FLOAT: {
        union { float f; int i; } v = { val->fval };
        emit(".long %d", v.i);
        break;
    }
    case KIND_DOUBLE: {
        union { double f; long i; } v = { val->fval };
        emit(".quad %ld", v.i);
        break;
    }
    case KIND_BOOL:
        emit(".byte %d", !!eval_intexpr(val));
        break;
    case KIND_CHAR:
        emit(".byte %d", eval_intexpr(val));
        break;
    case KIND_SHORT:
        emit(".short %d", eval_intexpr(val));
        break;
    case KIND_INT:
        emit(".long %d", eval_intexpr(val));
        break;
    case KIND_LONG:
    case KIND_LLONG:
    case KIND_PTR: {
        bool is_char_ptr = (val->operand->ty->kind == KIND_ARRAY && val->operand->ty->ptr->kind == KIND_CHAR);
        if (is_char_ptr) {
            emit_data_charptr(val->operand->sval, depth);
	} else if (val->kind == AST_GVAR) {
            emit(".quad %s", val->glabel);
	} else {
            emit(".quad %d", eval_intexpr(val));
	}
        break;
    }
    default:
        error("don't know how to handle\n  <%s>\n  <%s>", c2s(ty), a2s(val));
    }
}

static void do_emit_data(Vector *inits, int size, int off, int depth) {
    SAVE;
    for (int i = 0; i < vec_len(inits) && 0 < size; i++) {
        Node *node = vec_get(inits, i);
        Node *v = node->initval;
        emit_padding(node, off);
        if (node->totype->bitsize > 0) {
            assert(node->totype->bitoff == 0);
            long data = eval_intexpr(v);
            Type *totype = node->totype;
            for (i++ ; i < vec_len(inits); i++) {
                node = vec_get(inits, i);
                if (node->totype->bitsize <= 0) {
                    break;
                }
                v = node->initval;
                totype = node->totype;
                data |= ((((long)1 << totype->bitsize) - 1) & eval_intexpr(v)) << totype->bitoff;
            }
            emit_data_primtype(totype, &(Node){ AST_LITERAL, totype, .ival = data }, depth);
            off += totype->size;
            size -= totype->size;
            if (i == vec_len(inits))
                break;
        } else {
            off += node->totype->size;
            size -= node->totype->size;
        }
        if (v->kind == AST_ADDR) {
            emit_data_addr(v->operand, depth);
            continue;
        }
        if (v->kind == AST_LVAR && v->lvarinit) {
            do_emit_data(v->lvarinit, v->ty->size, 0, depth);
            continue;
        }
        emit_data_primtype(node->totype, node->initval, depth);
    }
    emit_zero(size);
}

static void emit_data(Node *v, int off, int depth) {
    SAVE;
    emit(".data %d", depth);
    if (!v->declvar->ty->isstatic)
        emit_noindent(".global %s", v->declvar->glabel);
    emit_noindent("%s:", v->declvar->glabel);
    do_emit_data(v->declinit, v->declvar->ty->size, off, depth);
}

static void emit_bss(Node *v) {
    SAVE;
    emit(".data");
    if (!v->declvar->ty->isstatic)
        emit(".global %s", v->declvar->glabel);
    emit(".lcomm %s, %d", v->declvar->glabel, v->declvar->ty->size);
}

static void emit_global_var(Node *v) {
    SAVE;
    if (v->declinit)
        emit_data(v, 0, 0);
    else
        emit_bss(v);
}

static int emit_regsave_area(void) {
    int pos = -REGAREA_SIZE;
    emit("mov #rdi, %d(#rsp)", pos);
    emit("mov #rsi, %d(#rsp)", (pos += 8));
    emit("mov #rdx, %d(#rsp)", (pos += 8));
    emit("mov #rcx, %d(#rsp)", (pos += 8));
    emit("mov #r8, %d(#rsp)", (pos += 8));
    emit("mov #r9, %d(#rsp)", pos + 8);
    char *end = make_label();
    for (int i = 0; i < 16; i++) {
        emit("test #al, #al");
        emit("jz %s", end);
        emit("movsd #xmm%d, %d(#rsp)", i, (pos += 16));
        emit("sub $1, #al");
    }
    emit_label(end);
    emit("sub $%d, #rsp", REGAREA_SIZE);
    return REGAREA_SIZE;
}

static void push_func_params(Vector *params, int off) {
    int ireg = 0;
    int xreg = 0;
    int arg = 2;
    for (int i = 0; i < vec_len(params); i++) {
        Node *v = vec_get(params, i);
        if (v->ty->kind == KIND_STRUCT) {
            emit("lea %d(#rbp), #rax", arg * 8);
            int size = push_struct(v->ty->size);
            off -= size;
            arg += size / 8;
        } else if (is_flotype(v->ty)) {
            if (xreg >= 8) {
                emit("mov %d(#rbp), #rax", arg++ * 8);
                push("rax");
            } else {
                push_xmm(xreg++);
            }
            off -= 8;
        } else {
            if (ireg >= 6) {
                if (v->ty->kind == KIND_BOOL) {
                    emit("mov %d(#rbp), #al", arg++ * 8);
                    emit("movzb #al, #eax");
                } else {
                    emit("mov %d(#rbp), #rax", arg++ * 8);
                }
                push("rax");
            } else {
                if (v->ty->kind == KIND_BOOL)
                    emit("movzb #%s, #%s", SREGS[ireg], MREGS[ireg]);
                push(REGS[ireg++]);
            }
            off -= 8;
        }
        v->loff = off;
    }
}

static void emit_func_prologue(Node *func) {
    SAVE;
    emit(".text");
    if (!func->ty->isstatic)
        emit_noindent(".global %s", func->fname);
    emit_noindent("%s:", func->fname);
    emit("nop");
    push("rbp");
    emit("mov #rsp, #rbp");
    int off = 0;
    if (func->ty->hasva) {
        set_reg_nums(func->params);
        off -= emit_regsave_area();
    }
    push_func_params(func->params, off);
    off -= vec_len(func->params) * 8;

    int localarea = 0;
    for (int i = 0; i < vec_len(func->localvars); i++) {
        Node *v = vec_get(func->localvars, i);
        int size = align(v->ty->size, 8);
        assert(size % 8 == 0);
        off -= size;
        v->loff = off;
        localarea += size;
    }
    if (localarea) {
        emit("sub $%d, #rsp", localarea);
        stackpos += localarea;
    }
}

void emit_toplevel(Node *v) {
    stackpos = 8;
    if (v->kind == AST_FUNC) {
        emit_func_prologue(v);
        emit_expr(v->body);
        emit_ret();
    } else if (v->kind == AST_DECL) {
        emit_global_var(v);
    } else {
        error("internal error");
    }
}
