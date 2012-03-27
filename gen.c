#include <stdarg.h>
#include "8cc.h"

static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static int TAB = 8;
static List *functions = &EMPTY_LIST;
static char *lbreak;
static char *lcontinue;
static char *lswitch;
static int stackpos;

static void emit_expr(Node *node);
static void emit_load_deref(Ctype *result_type, Ctype *operand_type, int off);

#define emit(...)        emitf(__LINE__, "\t" __VA_ARGS__)
#define emit_noindent(...)  emitf(__LINE__, __VA_ARGS__)

#define SAVE                                                    \
    int save_hook __attribute__((cleanup(pop_function)));       \
    list_push(functions, (void *)__func__)

static void pop_function(void *ignore) {
    list_pop(functions);
}

static char *get_caller_list(void) {
    String *s = make_string();
    for (Iter *i = list_iter(functions); !iter_end(i);) {
        string_appendf(s, "%s", iter_next(i));
        if (!iter_end(i))
            string_appendf(s, " -> ");
    }
    return get_cstring(s);
}

static void emitf(int line, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int col = vprintf(fmt, args);
    va_end(args);

    for (char *p = fmt; *p; p++)
        if (*p == '\t')
            col += TAB - 1;
    int space = (28 - col) > 0 ? (30 - col) : 2;
    printf("%*c %s:%d\n", space, '#', get_caller_list(), line);
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

static void emit_gload(Ctype *ctype, char *label, int off) {
    SAVE;
    if (ctype->type == CTYPE_ARRAY) {
        if (off)
            emit("lea %s+%d(%%rip), %%rax", label, off);
        else
            emit("lea %s(%%rip), %%rax", label);
        return;
    }
    char *reg = get_int_reg(ctype, 'a');
    if (ctype->size < 4)
        emit("mov $0, %%eax");
    if (off)
        emit("mov %s+%d(%%rip), %%%s", label, off, reg);
    else
        emit("mov %s(%%rip), %%%s", label, reg);
}

static void emit_toint(Ctype *ctype) {
    SAVE;
    if (!is_flotype(ctype))
        return;
    emit("cvttsd2si %%xmm0, %%eax");
}

static void emit_todouble(Ctype *ctype) {
    SAVE;
    if (is_flotype(ctype))
        return;
    emit("cvtsi2sd %%eax, %%xmm0");
}

static void emit_lload(Ctype *ctype, int off) {
    SAVE;
    if (ctype->type == CTYPE_ARRAY) {
        emit("lea %d(%%rbp), %%rax", off);
    } else if (ctype->type == CTYPE_FLOAT) {
        emit("cvtps2pd %d(%%rbp), %%xmm0", off);
    } else if (ctype->type == CTYPE_DOUBLE || ctype->type == CTYPE_LDOUBLE) {
        emit("movsd %d(%%rbp), %%xmm0", off);
    } else {
        char *reg = get_int_reg(ctype, 'a');
        if (ctype->size < 4)
            emit("mov $0, %%eax");
        emit("mov %d(%%rbp), %%%s", off, reg);
    }
}

static void emit_gsave(char *varname, Ctype *ctype, int off) {
    SAVE;
    assert(ctype->type != CTYPE_ARRAY);
    char *reg = get_int_reg(ctype, 'a');
    if (off)
        emit("mov %%%s, %s+%d(%%rip)", reg, varname, off);
    else
        emit("mov %%%s, %s(%%rip)", reg, varname);
}

static void emit_lsave(Ctype *ctype, int off) {
    SAVE;
    if (ctype->type == CTYPE_FLOAT) {
        push_xmm(0);
        emit("cvtpd2ps %%xmm0, %%xmm0");
        emit("movss %%xmm0, %d(%%rbp)", off);
        pop_xmm(0);
    } else if (ctype->type == CTYPE_DOUBLE || ctype->type == CTYPE_LDOUBLE) {
        emit("movsd %%xmm0, %d(%%rbp)", off);
    } else {
        char *reg = get_int_reg(ctype, 'a');
        emit("mov %%%s, %d(%%rbp)", reg, off);
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

static void emit_pointer_arith(char op, Node *left, Node *right) {
    SAVE;
    emit_expr(left);
    push("rax");
    emit_expr(right);
    int size = left->ctype->ptr->size;
    if (size > 1)
        emit("imul $%d, %%rax", size);
    emit("mov %%rax, %%rcx");
    pop("rax");
    emit("add %%rcx, %%rax");
}

static void emit_assign_struct_ref(Node *struc, Ctype *field, int off) {
    SAVE;
    switch (struc->type) {
    case AST_LVAR:
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
        emit_lload(field, struc->loff + field->offset + off);
        break;
    case AST_GVAR:
        emit_gload(field, struc->varname, field->offset + off);
        break;
    case AST_STRUCT_REF:
        emit_load_struct_ref(struc->struc, field, struc->ctype->offset + off);
        break;
    case AST_DEREF:
        emit_expr(struc->operand);
        emit_load_deref(field, field, field->offset + off);
        break;
    default:
        error("internal error: %s", a2s(struc));
    }
}

static void emit_assign(Node *var) {
    SAVE;
    switch (var->type) {
    case AST_DEREF: emit_assign_deref(var); break;
    case AST_STRUCT_REF: emit_assign_struct_ref(var->struc, var->ctype, 0); break;
    case AST_LVAR: emit_lsave(var->ctype, var->loff); break;
    case AST_GVAR: emit_gsave(var->varname, var->ctype, 0); break;
    default: error("internal error");
    }
}

static void emit_comp(char *inst, Node *node) {
    SAVE;
    if (is_flotype(node->ctype)) {
        emit_expr(node->left);
        emit_todouble(node->left->ctype);
        push_xmm(0);
        emit_expr(node->right);
        emit_todouble(node->right->ctype);
        pop_xmm(1);
        emit("ucomisd %%xmm0, %%xmm1");
    } else {
        emit_expr(node->left);
        emit_toint(node->left->ctype);
        push("rax");
        emit_expr(node->right);
        emit_toint(node->right->ctype);
        pop("rcx");
        emit("cmp %%rax, %%rcx");
    }
    emit("%s %%al", inst);
    emit("movzb %%al, %%eax");
}

static void emit_binop_int_arith(Node *node) {
    SAVE;
    char *op;
    switch (node->type) {
    case '+': op = "add"; break;
    case '-': op = "sub"; break;
    case '*': op = "imul"; break;
    case '^': op = "xor"; break;
    case OP_LSH: op = "sal"; break;
    case OP_RSH: op = "sar"; break;
    case '/': case '%': break;
    default: error("invalid operator '%d'", node->type);
    }
    emit_expr(node->left);
    emit_toint(node->left->ctype);
    push("rax");
    emit_expr(node->right);
    emit_toint(node->right->ctype);
    emit("mov %%rax, %%rcx");
    pop("rax");
    if (node->type == '/' || node->type == '%') {
        emit("mov $0, %%edx");
        emit("idiv %%rcx");
        if (node->type == '%')
            emit("mov %%edx, %%eax");
    } else if (node->type == OP_LSH || node->type == OP_RSH) {
        emit("%s %%cl, %%rax", op);
    } else {
        emit("%s %%rcx, %%rax", op);
    }
}

static void emit_binop_float_arith(Node *node) {
    SAVE;
    char *op;
    switch (node->type) {
    case '+': op = "addsd"; break;
    case '-': op = "subsd"; break;
    case '*': op = "mulsd"; break;
    case '/': op = "divsd"; break;
    default: error("invalid operator '%d'", node->type);
    }
    emit_expr(node->left);
    emit_todouble(node->left->ctype);
    push_xmm(0);
    emit_expr(node->right);
    emit_todouble(node->right->ctype);
    emit("movsd %%xmm0, %%xmm1");
    pop_xmm(0);
    emit("%s %%xmm1, %%xmm0", op);
}

static void emit_load_convert(Ctype *to, Ctype *from) {
    SAVE;
    if (is_flotype(to))
        emit_todouble(from);
    else
        emit_toint(from);
}

static void emit_save_convert(Ctype *to, Ctype *from) {
    SAVE;
    if (is_inttype(from) && to->type == CTYPE_FLOAT)
        emit("cvtsi2ss %%eax, %%xmm0");
    else if (is_flotype(from) && to->type == CTYPE_FLOAT)
        emit("cvtpd2ps %%xmm0, %%xmm0");
    else if (is_inttype(from) && (to->type == CTYPE_DOUBLE || to->type == CTYPE_LDOUBLE))
        emit("cvtsi2sd %%eax, %%xmm0");
    else if (!(is_flotype(from) && (to->type == CTYPE_DOUBLE || to->type == CTYPE_LDOUBLE)))
        emit_load_convert(to, from);
}

static void emit_binop(Node *node) {
    SAVE;
    if (node->type == '=') {
        emit_expr(node->right);
        emit_load_convert(node->ctype, node->right->ctype);
        emit_assign(node->left);
        return;
    }
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

static void emit_inc_dec(Node *node, char *op) {
    SAVE;
    emit_expr(node->operand);
    push("rax");
    emit("%s $1, %%rax", op);
    emit_assign(node->operand);
    pop("rax");
}

static void emit_load_deref(Ctype *result_type, Ctype *operand_type, int off) {
    SAVE;
    if (operand_type->type == CTYPE_PTR &&
        operand_type->ptr->type == CTYPE_ARRAY)
        return;
    char *reg = get_int_reg(result_type, 'c');
    if (result_type->size < 4)
        emit("mov $0, %%ecx");
    if (off)
        emit("mov %d(%%rax), %%%s", off, reg);
    else
        emit("mov (%%rax), %%%s", reg);
    emit("mov %%rcx, %%rax");
}

static List *get_arg_types(Node *node) {
    List *r = make_list();
    for (Iter *i = list_iter(node->args), *j = list_iter(node->paramtypes);
         !iter_end(i);) {
        Node *v = iter_next(i);
        Ctype *ptype = iter_next(j);
        list_push(r, ptype ? ptype : result_type('=', v->ctype, ctype_int));
    }
    return r;
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

static void emit_expr(Node *node) {
    SAVE;
    switch (node->type) {
    case AST_LITERAL:
        switch (node->ctype->type) {
        case CTYPE_CHAR:
            emit("mov $%d, %%rax", node->ival);
            break;
        case CTYPE_INT:
            emit("mov $%d, %%eax", node->ival);
            break;
        case CTYPE_LONG:
        case CTYPE_LLONG:
            emit("mov $%lu, %%rax", (unsigned long)node->ival);
            break;
        case CTYPE_FLOAT:
        case CTYPE_DOUBLE:
        case CTYPE_LDOUBLE:
            emit("movsd %s(%%rip), %%xmm0", node->flabel);
            break;
        default:
            error("internal error");
        }
        break;
    case AST_STRING:
        emit("lea %s(%%rip), %%rax", node->slabel);
        break;
    case AST_LVAR:
        emit_lload(node->ctype, node->loff);
        break;
    case AST_GVAR:
        emit_gload(node->ctype, node->glabel, 0);
        break;
    case AST_FUNCALL: {
        int ireg = 0;
        int xreg = 0;
        List *argtypes = get_arg_types(node);
        for (Iter *i = list_iter(argtypes); !iter_end(i);) {
            if (is_flotype(iter_next(i))) {
                if (xreg > 0) push_xmm(xreg);
                xreg++;
            } else {
                push(REGS[ireg++]);
            }
        }
        for (Iter *i = list_iter(node->args), *j = list_iter(argtypes);
             !iter_end(i);) {
            Node *v = iter_next(i);
            emit_expr(v);
            Ctype *ptype = iter_next(j);
            emit_save_convert(ptype, v->ctype);
            if (is_flotype(ptype))
                push_xmm(0);
            else
                push("rax");
        }
        int ir = ireg;
        int xr = xreg;
        for (Iter *i = list_iter(list_reverse(argtypes)); !iter_end(i);) {
            if (is_flotype(iter_next(i)))
                pop_xmm(--xr);
            else
                pop(REGS[--ir]);
        }
        emit("mov $%d, %%eax", xreg);
        if (stackpos % 16)
            emit("sub $8, %%rsp");
        emit("call %s", node->fname);
        if (stackpos % 16)
            emit("add $8, %%rsp");
        for (Iter *i = list_iter(list_reverse(argtypes)); !iter_end(i);) {
            if (is_flotype(iter_next(i))) {
                if (xreg != 1)
                    pop_xmm(--xreg);
            } else {
                pop(REGS[--ireg]);
            }
        }
        if (node->ctype->type == CTYPE_FLOAT)
            emit("cvtps2pd %%xmm0, %%xmm0");
        break;
    }
    case AST_DECL: {
        if (!node->declinit)
            return;
        if (node->declinit->type == AST_INIT_LIST) {
            int off = 0;
            for (Iter *iter = list_iter(node->declinit->initlist); !iter_end(iter);) {
                Node *elem = iter_next(iter);
                emit_expr(elem);
                emit_lsave(elem->totype, node->declvar->loff + off);
                off += elem->totype->size;
            }
        } else if (node->declvar->ctype->type == CTYPE_ARRAY) {
            assert(node->declinit->type == AST_STRING);
            int i = 0;
            for (char *p = node->declinit->sval; *p; p++, i++)
                emit("movb $%d, %d(%%rbp)", *p, node->declvar->loff + i);
            emit("movb $0, %d(%%rbp)", node->declvar->loff + i);
        } else if (node->declinit->type == AST_STRING) {
            emit_gload(node->declinit->ctype, node->declinit->slabel, 0);
            emit_lsave(node->declvar->ctype, node->declvar->loff);
        } else {
            emit_expr(node->declinit);
            emit_lsave(node->declvar->ctype, node->declvar->loff);
        }
        return;
    }
    case AST_ADDR:
        switch (node->operand->type) {
        case AST_LVAR:
            emit("lea %d(%%rbp), %%rax", node->operand->loff);
            break;
        case AST_GVAR:
            emit("lea %s(%%rip), %%rax", node->operand->glabel);
            break;
        default:
            error("internal error");
        }
        break;
    case AST_DEREF:
        emit_expr(node->operand);
        emit_load_deref(node->ctype, node->operand->ctype, 0);
        break;
    case AST_IF:
    case AST_TERNARY: {
        emit_expr(node->cond);
        char *ne = make_label();
        emit_je(ne);
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
        break;
    }
    case AST_FOR: {

#define SET_JUMP_LABELS(brk, cont)              \
        char *obreak = lbreak;                  \
        char *ocontinue = lcontinue;            \
        lbreak = brk;                           \
        lcontinue = cont
#define RESTORE_JUMP_LABELS()                   \
        lbreak = obreak;                        \
        lcontinue = ocontinue;

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
        emit_expr(node->forbody);
        emit_label(step);
        if (node->forstep)
            emit_expr(node->forstep);
        emit_jmp(begin);
        emit_label(end);
        RESTORE_JUMP_LABELS();
        break;
    }
    case AST_WHILE: {
        char *begin = make_label();
        char *end = make_label();
        SET_JUMP_LABELS(end, begin);
        emit_label(begin);
        emit_expr(node->forcond);
        emit_je(end);
        emit_expr(node->forbody);
        emit_jmp(begin);
        emit_label(end);
        RESTORE_JUMP_LABELS();
        break;
    }
    case AST_DO: {
        char *begin = make_label();
        char *end = make_label();
        SET_JUMP_LABELS(end, begin);
        emit_label(begin);
        emit_expr(node->forbody);
        emit_expr(node->forcond);
        emit_je(end);
        emit_jmp(begin);
        emit_label(end);
        RESTORE_JUMP_LABELS();
        break;
#undef SET_JUMP_LABELS
#undef RESTORE_JUMP_LABELS
    }
    case AST_SWITCH: {
        char *oswitch = lswitch, *obreak = lbreak;
        emit_expr(node->switchexpr);
        lswitch = make_label();
        lbreak = make_label();
        emit_jmp(lswitch);
        emit_expr(node->switchbody);
        emit_label(lswitch);
        emit_label(lbreak);
        lswitch = oswitch;
        lbreak = obreak;
        break;
    }
    case AST_CASE: {
        if (!lswitch)
            error("stray case label");
        emit_label(lswitch);
        emit("cmp $%d, %%eax", node->caseval);
        lswitch = make_label();
        emit("jne %s", lswitch);
        break;
    }
    case AST_DEFAULT:
        if (!lswitch)
            error("stray case label");
        emit_label(lswitch);
        lswitch = make_label();
        break;
    case AST_GOTO:
        assert(node->newlabel);
        emit_jmp(node->newlabel);
        break;
    case AST_LABEL:
        if (node->newlabel)
            emit_label(node->newlabel);
        break;
    case AST_RETURN:
        if (node->retval) {
            emit_expr(node->retval);
            emit_save_convert(node->ctype, node->retval->ctype);
        }
        emit("leave");
        emit("ret");
        break;
    case AST_BREAK:
        if (!lbreak)
            error("stray break statement");
        emit_jmp(lbreak);
        break;
    case AST_CONTINUE:
        if (!lcontinue)
            error("stray continue statement");
        emit_jmp(lcontinue);
        break;
    case AST_COMPOUND_STMT:
        for (Iter *i = list_iter(node->stmts); !iter_end(i);)
            emit_expr(iter_next(i));
        break;
    case AST_STRUCT_REF:
        emit_load_struct_ref(node->struc, node->ctype, 0);
        break;
    case OP_INC:
        emit_inc_dec(node, "add");
        break;
    case OP_DEC:
        emit_inc_dec(node, "sub");
        break;
    case '!':
        emit_expr(node->operand);
        emit("cmp $0, %%rax");
        emit("sete %%al");
        emit("movzb %%al, %%eax");
        break;
    case '&':
        emit_expr(node->left);
        push("rax");
        emit_expr(node->right);
        pop("rcx");
        emit("and %%rcx, %%rax");
        break;
    case '|':
        emit_expr(node->left);
        push("rax");
        emit_expr(node->right);
        pop("rcx");
        emit("or %%rcx, %%rax");
        break;
    case '~':
        emit_expr(node->left);
        emit("not %%rax");
        break;
    case OP_LOGAND: {
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
        break;
    }
    case OP_LOGOR: {
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
        break;
    }
    case OP_CAST: {
        emit_expr(node->operand);
        emit_load_convert(node->ctype, node->operand->ctype);
        break;
    }
    default:
        emit_binop(node);
    }
}

static void emit_data_int(Node *data) {
    SAVE;
    assert(data->ctype->type != CTYPE_ARRAY);
    switch (data->ctype->size) {
    case 1: emit(".byte %d", data->ival); break;
    case 4: emit(".long %d", data->ival); break;
    case 8: emit(".quad %d", data->ival); break;
    default: error("internal error");
    }
}

static void emit_data(Node *v) {
    SAVE;
    emit_noindent(".global %s", v->declvar->varname);
    emit_noindent("%s:", v->declvar->varname);
    if (v->declinit->type == AST_INIT_LIST) {
        for (Iter *iter = list_iter(v->declinit->initlist); !iter_end(iter);) {
            emit_data_int(iter_next(iter));
        }
        return;
    }
    assert(v->declinit->type == AST_LITERAL && is_inttype(v->declinit->ctype));
    emit_data_int(v->declinit);
}

static void emit_bss(Node *v) {
    SAVE;
    emit(".lcomm %s, %d", v->declvar->varname, v->declvar->ctype->size);
}

static void emit_global_var(Node *v) {
    SAVE;
    if (v->declinit)
        emit_data(v);
    else
        emit_bss(v);
}

void emit_data_section(void) {
    SAVE;
    emit(".data");
    for (Iter *i = list_iter(strings); !iter_end(i);) {
        Node *v = iter_next(i);
        emit_noindent("%s:", v->slabel);
        emit(".string \"%s\"", quote_cstring(v->sval));
    }
    for (Iter *i = list_iter(flonums); !iter_end(i);) {
        Node *v = iter_next(i);
        char *label = make_label();
        v->flabel = label;
        emit_noindent("%s:", label);
        emit(".long %d", ((int *)&v->fval)[0]);
        emit(".long %d", ((int *)&v->fval)[1]);
    }
}

static int align(int n, int m) {
    int rem = n % m;
    return (rem == 0) ? n : n - rem + m;
}

static void emit_func_prologue(Node *func) {
    SAVE;
    emit(".text");
    emit_noindent(".global %s", func->fname);
    emit_noindent("%s:", func->fname);
    push("rbp");
    emit("mov %%rsp, %%rbp");
    int off = 0;
    int ireg = 0;
    int xreg = 0;
    for (Iter *i = list_iter(func->params); !iter_end(i);) {
        Node *v = iter_next(i);
        if (v->ctype->type == CTYPE_FLOAT) {
            push_xmm(xreg++);
        } else if (v->ctype->type == CTYPE_DOUBLE || v->ctype->type == CTYPE_LDOUBLE) {
            push_xmm(xreg++);
        } else {
            push(REGS[ireg++]);
        }
        off -= align(v->ctype->size, 8);
        v->loff = off;
    }
    int localarea = 0;
    for (Iter *i = list_iter(func->localvars); !iter_end(i);) {
        Node *v = iter_next(i);
        off -= align(v->ctype->size, 8);
        v->loff = off;
        localarea += off;
    }
    if (localarea)
        emit("sub $%d, %%rsp", -localarea);
    stackpos += -(off - 8);
}

static void emit_func_epilogue(void) {
    SAVE;
    emit("leave");
    emit("ret");
}

void emit_toplevel(Node *v) {
    stackpos = 0;
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
