// Copyright 2014 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include <assert.h>
#include <stdlib.h>
#include "8cc.h"

static Vector *mems = &EMPTY_VECTOR;
static Map *lvars = &EMPTY_MAP;
static Vector *vars = &EMPTY_VECTOR;
static Vector *insts = &EMPTY_VECTOR;
static int offset;
static FILE *out;

#define NUMREGS 6

static char *regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };

enum { VAR, LIT };
enum { I32 };
enum { ADD, MUL, RET, ALLOC, LOAD, STORE };

#define MAX(x, y) ((x) > (y) ? (x) : (y))

typedef struct {
    int off;
    int size;
} Mem;

typedef struct {
    int kind;
    union {
        // VAR
        struct {
            int id;
            int off;
            bool spilled;
        };
        // LIT
        int val;
    };
} Var;

typedef struct {
    int kind;
    void *arg1;
    void *arg2;
    void *arg3;
    void *arg4;
    int size; // ALLOC
} Inst;

typedef struct {
    char *name;
    Vector *insts;
} Func;

/*
 * Constructors
 */

static Mem* make_mem(int size) {
    Mem *r = malloc(sizeof(Mem));
    r->size = size;
    offset += size;
    r->off = -offset;
    return r;
}

static Var *make_var(void) {
    static int id = 1;
    Var *r = malloc(sizeof(Var));
    r->kind = VAR;
    r->id = id++;
    vec_push(vars, r);
    return r;
}

static Var *make_literal(int val) {
    Var *r = malloc(sizeof(Var));
    r->kind = LIT;
    r->val = val;
    return r;
}

static Inst *make_inst(Inst *in) {
    Inst *r = malloc(sizeof(Inst));
    *r = *in;
    return r;
}

static Func *make_func(char *name, Vector *insts) {
    Func *r = malloc(sizeof(Func));
    r->name = name;
    r->insts = insts;
    return r;
}

/*
 * Node -> Inst
 */

static void emit(Inst *in) {
    vec_push(insts, in);
}

static Var *alloc(Type *ty) {
    int size = MAX(ty->size, 8);
    Mem *m = make_mem(size);
    vec_push(mems, m);
    Var *v = make_var();
    emit(make_inst(&(Inst){ ALLOC, v, m }));
    return v;
}

static Var *walk(Node *node, bool lval) {
    switch (node->kind) {
    case AST_DECL: {
        Var *ptr = alloc(node->declvar->ty);
        map_put(lvars, node->declvar->varname, ptr);
        if (node->declinit && vec_len(node->declinit) > 0) {
            Node *init = vec_get(node->declinit, 0);
            Var *rhs = walk(init->initval, false);
            emit(make_inst(&(Inst){ STORE, ptr, rhs }));
        }
        return NULL;
    }
    case AST_LVAR: {
        Var *ptr = map_get(lvars, node->varname);
        assert(ptr);
        if (node->ty->kind == KIND_ARRAY)
            return ptr;
        if (lval)
            return ptr;
        Var *r = make_var();
        emit(make_inst(&(Inst){ LOAD, r, ptr }));
        return r;
    }
    case AST_COMPOUND_STMT: {
        Vector *body = node->stmts;
        for (int i = 0; i < vec_len(body); i++)
            walk(vec_get(body, i), false);
        return NULL;
    }
    case AST_RETURN: {
        Var *v = walk(node->retval, false);
        emit(make_inst(&(Inst){ RET, v }));
        return NULL;
    }
    case AST_CONV:
        return walk(node->operand, false);
    case AST_DEREF: {
        Var *ptr = walk(node->operand, lval);
        if (lval)
            return ptr;
        Var *r = make_var();
        emit(make_inst(&(Inst){ LOAD, r, ptr }));
        return r;
    }
    case '+': {
        Var *dst = make_var();
        Var *lhs = walk(node->left, false);
        Var *rhs = walk(node->right, false);
        if (node->left->ty->kind == KIND_PTR) {
            Var *tmp = make_var();
            Var *size = make_literal(node->left->ty->ptr->size);
            emit(make_inst(&(Inst){ MUL, tmp, rhs, size }));
            rhs = tmp;
        }
        emit(make_inst(&(Inst){ ADD, dst, lhs, rhs }));
        return dst;
    }
    case '*': {
        Var *dst = make_var();
        Var *lhs = walk(node->left, false);
        Var *rhs = walk(node->right, false);
        emit(make_inst(&(Inst){ MUL, dst, lhs, rhs }));
        return dst;
    }
    case '=': {
        Var *lhs = walk(node->left, true);
        Var *rhs = walk(node->right, false);
        emit(make_inst(&(Inst) { STORE, lhs, rhs }));
        return rhs;
    }
    case AST_LITERAL:
        assert(node->ty->kind == KIND_INT);
        return make_literal(node->ival);
    default:
        error("unknown node: %s", node2s(node));
    }
}

static Func *translate(Vector *toplevels) {
    assert(vec_len(toplevels) == 1);
    Node *node = vec_head(toplevels);
    assert(node->kind == AST_FUNC);
    char *name = node->fname;
    walk(node->body, false);
    return make_func(name, insts);
}

/*
 * Register allocator
 *
 * We don't have liveness analysis.
 * Each temporary variable is allocated to the stack
 * even if it's not going to be spilled.
 * Machine registers are used as a cache.
 */

static void write_noindent(char *fmt, ...);
static void write(char *fmt, ...);

typedef struct {
    Var *v;
    char *reg;
} Regmap;

static Regmap reg2var[6];

static void move_to_head(int i) {
    // Avoid using struct assignment because it doesn't
    // work with the current (old) code generator.
    Var *v = reg2var[i].v;
    char *reg = reg2var[i].reg;
    for (int j = i; j > 0; j--) {
        reg2var[j].v = reg2var[j-1].v;
        reg2var[j].reg = reg2var[j-1].reg;
    }
    reg2var[0].v = v;
    reg2var[0].reg = reg;
}

static char *regname(Var *v) {
    // Check if it's cached
    for (int i = 0; i < NUMREGS; i++) {
        if (reg2var[i].v != v)
            continue;
        move_to_head(i);
        return reg2var[0].reg;
    }
    // Look for an empty slot
    for (int i = 0; i < NUMREGS; i++) {
        if (reg2var[i].v)
            continue;
        reg2var[i].v = v;
        reg2var[i].reg = regs[i];
        move_to_head(i);
        return regs[i];
    }
    // Spill the least-recently used variable
    Regmap *last = &reg2var[NUMREGS - 1];
    char *reg = last->reg;
    write("movq %%%s, %d(%%rbp)  # spill", reg, last->v->off);
    last->v->spilled = true;
    if (v->spilled)
        write("movq %d(%%rbp), %%%s  # load", v->off, reg);
    last->v = v;
    last->reg = reg;
    move_to_head(NUMREGS - 1);
    return reg;
}

/*
 * Inst -> x86-64 assembly
 */

static char *str(Var *v) {
    switch (v->kind) {
    case VAR:
        return format("%%%s", regname(v));
    case LIT:
        return format("$%d", v->val);
    default:
        error("internal error");
    }
}

static void write_noindent(char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
}

static void write(char *fmt, ...) {
    fprintf(out, "    ");
    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
}

static void print(Func *f) {
    for (int i = 0; i < vec_len(f->insts); i++) {
        Inst *in = vec_get(f->insts, i);
        switch (in->kind) {
        case ADD:
            write("# add");
            write("movq %s, %s", str(in->arg2), str(in->arg1));
            write("addq %s, %s", str(in->arg3), str(in->arg1));
            break;
        case ALLOC: {
            Var *ptr = in->arg1;
            Mem *mem = in->arg2;
            write("# alloc");
            write("leaq %d(%%rsp), %s", mem->off, str(ptr));
            break;
        }
        case MUL:
            write("# mul");
            write("movq %s, %s", str(in->arg2), str(in->arg1));
            write("imulq %s, %s", str(in->arg3), str(in->arg1));
            break;
        case RET:
            write("# ret");
            write("movq %s, %%rax", str(in->arg1));
            write("jmp end");
            break;
        case LOAD: {
            write("# load");
            Var *var = in->arg1;
            Var *ptr = in->arg2;
            write("movq (%s), %s", str(ptr), str(var));
            break;
        }
        case STORE: {
            write("# store");
            Var *ptr = in->arg1;
            Var *var = in->arg2;
            write("movq %s, (%s)", str(var), str(ptr));
            break;
        }
        default:
            error("internal error");
        }
    }
}

static void regalloc(void) {
    for (int i = 0; i < vec_len(vars); i++) {
        Var *v = vec_get(vars, i);
        offset += 8;
        v->off = -offset;
    }
}

static void compile(Func *f) {
    regalloc();
    write_noindent(".text");
    write_noindent(".globl %s", f->name);
    write_noindent("%s:", f->name);
    write("push %%rbp");
    write("mov %%rsp, %%rbp");
    write("sub $%d, %%rsp", offset);
    print(f);
    write("end:");
    write("add $%d, %%rsp", offset);
    write("popq %%rbp");
    write("ret");
}

void codegen(Vector *toplevels, FILE *fp) {
    out = fp;
    Func *f = translate(toplevels);
    compile(f);
}
