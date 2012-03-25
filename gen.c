#include <stdarg.h>
#include "8cc.h"

static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static int TAB = 8;
static List *functions = &EMPTY_LIST;

static int stackpos;

static void emit_expr(Ast *ast);
static void emit_load_deref(Ctype *result_type, Ctype *operand_type, int off);

#define emit(...)        emitf(__LINE__, "\t" __VA_ARGS__)
#define emit_label(...)  emitf(__LINE__, __VA_ARGS__)

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

static void emit_assign_deref(Ast *var) {
    SAVE;
    push("rax");
    emit_expr(var->operand);
    emit_assign_deref_int(var->operand->ctype->ptr, 0);
}

static void emit_pointer_arith(char op, Ast *left, Ast *right) {
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

static void emit_assign_struct_ref(Ast *struc, Ctype *field, int off) {
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

static void emit_load_struct_ref(Ast *struc, Ctype *field, int off) {
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

static void emit_assign(Ast *var) {
    SAVE;
    switch (var->type) {
    case AST_DEREF: emit_assign_deref(var); break;
    case AST_STRUCT_REF: emit_assign_struct_ref(var->struc, var->ctype, 0); break;
    case AST_LVAR: emit_lsave(var->ctype, var->loff); break;
    case AST_GVAR: emit_gsave(var->varname, var->ctype, 0); break;
    default: error("internal error");
    }
}

static void emit_comp(char *inst, Ast *ast) {
    SAVE;
    if (is_flotype(ast->ctype)) {
        emit_expr(ast->left);
        emit_todouble(ast->left->ctype);
        push_xmm(0);
        emit_expr(ast->right);
        emit_todouble(ast->right->ctype);
        pop_xmm(1);
        emit("ucomisd %%xmm0, %%xmm1");
    } else {
        emit_expr(ast->left);
        emit_toint(ast->left->ctype);
        push("rax");
        emit_expr(ast->right);
        emit_toint(ast->right->ctype);
        pop("rcx");
        emit("cmp %%rax, %%rcx");
    }
    emit("%s %%al", inst);
    emit("movzb %%al, %%eax");
}

static void emit_binop_int_arith(Ast *ast) {
    SAVE;
    char *op;
    switch (ast->type) {
    case '+': op = "add"; break;
    case '-': op = "sub"; break;
    case '*': op = "imul"; break;
    case '^': op = "xor"; break;
    case '/': case '%': break;
    default: error("invalid operator '%d'", ast->type);
    }
    emit_expr(ast->left);
    emit_toint(ast->left->ctype);
    push("rax");
    emit_expr(ast->right);
    emit_toint(ast->right->ctype);
    emit("mov %%rax, %%rcx");
    pop("rax");
    if (ast->type == '/' || ast->type == '%') {
        emit("mov $0, %%edx");
        emit("idiv %%rcx");
        if (ast->type == '%')
            emit("mov %%edx, %%eax");
    } else {
        emit("%s %%rcx, %%rax", op);
    }
}

static void emit_binop_float_arith(Ast *ast) {
    SAVE;
    char *op;
    switch (ast->type) {
    case '+': op = "addsd"; break;
    case '-': op = "subsd"; break;
    case '*': op = "mulsd"; break;
    case '/': op = "divsd"; break;
    default: error("invalid operator '%d'", ast->type);
    }
    emit_expr(ast->left);
    emit_todouble(ast->left->ctype);
    push_xmm(0);
    emit_expr(ast->right);
    emit_todouble(ast->right->ctype);
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

static void emit_binop(Ast *ast) {
    SAVE;
    if (ast->type == '=') {
        emit_expr(ast->right);
        emit_load_convert(ast->ctype, ast->right->ctype);
        emit_assign(ast->left);
        return;
    }
    if (ast->ctype->type == CTYPE_PTR) {
        emit_pointer_arith(ast->type, ast->left, ast->right);
        return;
    }
    switch (ast->type) {
    case '<': emit_comp("setl", ast); return;
    case '>': emit_comp("setg", ast); return;
    case OP_EQ: emit_comp("sete", ast); return;
    case OP_GE: emit_comp("setge", ast); return;
    case OP_LE: emit_comp("setle", ast); return;
    case OP_NE: emit_comp("setne", ast); return;
    }
    if (is_inttype(ast->ctype))
        emit_binop_int_arith(ast);
    else if (is_flotype(ast->ctype))
        emit_binop_float_arith(ast);
    else
        error("internal error");
}

static void emit_inc_dec(Ast *ast, char *op) {
    SAVE;
    emit_expr(ast->operand);
    push("rax");
    emit("%s $1, %%rax", op);
    emit_assign(ast->operand);
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

static List *get_arg_types(Ast *ast) {
    List *r = make_list();
    for (Iter *i = list_iter(ast->args), *j = list_iter(ast->paramtypes);
         !iter_end(i);) {
        Ast *v = iter_next(i);
        Ctype *ptype = iter_next(j);
        list_push(r, ptype ? ptype : result_type('=', v->ctype, ctype_int));
    }
    return r;
}

static void emit_expr(Ast *ast) {
    SAVE;
    switch (ast->type) {
    case AST_LITERAL:
        switch (ast->ctype->type) {
        case CTYPE_CHAR:
            emit("mov $%d, %%rax", ast->ival);
            break;
        case CTYPE_INT:
            emit("mov $%d, %%eax", ast->ival);
            break;
        case CTYPE_LONG:
            emit("mov $%lu, %%rax", (unsigned long)ast->ival);
            break;
        case CTYPE_FLOAT:
        case CTYPE_DOUBLE:
        case CTYPE_LDOUBLE:
            emit("movsd %s(%%rip), %%xmm0", ast->flabel);
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
        int ireg = 0;
        int xreg = 0;
        List *argtypes = get_arg_types(ast);
        for (Iter *i = list_iter(argtypes); !iter_end(i);) {
            if (is_flotype(iter_next(i))) {
                if (xreg > 0) push_xmm(xreg);
                xreg++;
            } else {
                push(REGS[ireg++]);
            }
        }
        for (Iter *i = list_iter(ast->args), *j = list_iter(argtypes);
             !iter_end(i);) {
            Ast *v = iter_next(i);
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
        emit("call %s", ast->fname);
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
        if (ast->ctype->type == CTYPE_FLOAT)
            emit("cvtps2pd %%xmm0, %%xmm0");
        break;
    }
    case AST_DECL: {
        if (!ast->declinit)
            return;
        if (ast->declinit->type == AST_INIT_LIST) {
            int off = 0;
            for (Iter *iter = list_iter(ast->declinit->initlist); !iter_end(iter);) {
                Ast *elem = iter_next(iter);
                emit_expr(elem);
                emit_lsave(elem->totype, ast->declvar->loff + off);
                off += elem->totype->size;
            }
        } else if (ast->declvar->ctype->type == CTYPE_ARRAY) {
            assert(ast->declinit->type == AST_STRING);
            int i = 0;
            for (char *p = ast->declinit->sval; *p; p++, i++)
                emit("movb $%d, %d(%%rbp)", *p, ast->declvar->loff + i);
            emit("movb $0, %d(%%rbp)", ast->declvar->loff + i);
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
            emit("lea %d(%%rbp), %%rax", ast->operand->loff);
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
        if (ast->retval) {
            emit_expr(ast->retval);
            emit_save_convert(ast->ctype, ast->retval->ctype);
        }
        emit("leave");
        emit("ret");
        break;
    case AST_COMPOUND_STMT:
        for (Iter *i = list_iter(ast->stmts); !iter_end(i);)
            emit_expr(iter_next(i));
        break;
    case AST_STRUCT_REF:
        emit_load_struct_ref(ast->struc, ast->ctype, 0);
        break;
    case OP_INC:
        emit_inc_dec(ast, "add");
        break;
    case OP_DEC:
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
        push("rax");
        emit_expr(ast->right);
        pop("rcx");
        emit("and %%rcx, %%rax");
        break;
    case '|':
        emit_expr(ast->left);
        push("rax");
        emit_expr(ast->right);
        pop("rcx");
        emit("or %%rcx, %%rax");
        break;
    case '~':
        emit_expr(ast->left);
        emit("not %%rax");
        break;
    case OP_LOGAND: {
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
    case OP_LOGOR: {
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
    case OP_CAST: {
        emit_expr(ast->operand);
        emit_load_convert(ast->ctype, ast->operand->ctype);
        break;
    }
    default:
        emit_binop(ast);
    }
}

static void emit_data_int(Ast *data) {
    SAVE;
    assert(data->ctype->type != CTYPE_ARRAY);
    switch (data->ctype->size) {
    case 1: emit(".byte %d", data->ival); break;
    case 4: emit(".long %d", data->ival); break;
    case 8: emit(".quad %d", data->ival); break;
    default: error("internal error");
    }
}

static void emit_data(Ast *v) {
    SAVE;
    emit_label(".global %s", v->declvar->varname);
    emit_label("%s:", v->declvar->varname);
    if (v->declinit->type == AST_INIT_LIST) {
        for (Iter *iter = list_iter(v->declinit->initlist); !iter_end(iter);) {
            emit_data_int(iter_next(iter));
        }
        return;
    }
    assert(v->declinit->type == AST_LITERAL && is_inttype(v->declinit->ctype));
    emit_data_int(v->declinit);
}

static void emit_bss(Ast *v) {
    SAVE;
    emit(".lcomm %s, %d", v->declvar->varname, v->declvar->ctype->size);
}

static void emit_global_var(Ast *v) {
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
        Ast *v = iter_next(i);
        emit_label("%s:", v->slabel);
        emit(".string \"%s\"", quote_cstring(v->sval));
    }
    for (Iter *i = list_iter(flonums); !iter_end(i);) {
        Ast *v = iter_next(i);
        char *label = make_label();
        v->flabel = label;
        emit_label("%s:", label);
        emit(".long %d", ((int *)&v->fval)[0]);
        emit(".long %d", ((int *)&v->fval)[1]);
    }
}

static int align(int n, int m) {
    int rem = n % m;
    return (rem == 0) ? n : n - rem + m;
}

static void emit_func_prologue(Ast *func) {
    SAVE;
    emit(".text");
    emit_label(".global %s", func->fname);
    emit_label("%s:", func->fname);
    push("rbp");
    emit("mov %%rsp, %%rbp");
    int off = 0;
    int ireg = 0;
    int xreg = 0;
    for (Iter *i = list_iter(func->params); !iter_end(i);) {
        Ast *v = iter_next(i);
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
        Ast *v = iter_next(i);
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

void emit_toplevel(Ast *v) {
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
