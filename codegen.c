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

static char *regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };

enum { VAR, LIT };
enum { I32, PTR };
enum { ADD, RET, ALLOC, LOAD, STORE };

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
            Mem *ptr;
            char *reg;
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
    int size; // ALLOC
} Inst;

typedef struct {
    char *name;
    Vector *insts;
} Func;

static Mem* make_mem(int size) {
    Mem *r = malloc(sizeof(Mem));
    r->size = size;
    offset += size;
    r->off = -offset;
    return r;
}

static Var *make_var(Mem *mem) {
    static int id = 1;
    Var *r = malloc(sizeof(Var));
    r->kind = VAR;
    r->id = id++;
    vec_push(vars, r);
    return r;
}

static Mem *alloc(int size) {
    Mem *m = make_mem(size);
    vec_push(mems, m);
    return m;
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

static Func *make_func(Func *f) {
    Func *r = malloc(sizeof(Func));
    *r = *f;
    return r;
}

static char *nextreg(void) {
    static int cnt = 0;
    if (cnt == sizeof(regs) / sizeof(regs[0]))
        error("register exhausted");
    return regs[cnt++];
}

static void emit(Inst *in) {
    vec_push(insts, in);
}

static Var *walk(Node *node) {
    switch (node->kind) {
    case AST_DECL: {
        Mem *m = alloc(MAX(node->declvar->ty->size, 8));
        map_put(lvars, node->declvar->varname, m);
        if (vec_len(node->declinit) > 0) {
            Node *init = vec_get(node->declinit, 0);
            Var *rhs = walk(init->initval);
            emit(make_inst(&(Inst){ STORE, m, rhs }));
        }
        return NULL;
    }
    case AST_LVAR: {
        Mem *m = map_get(lvars, node->varname);
        assert(m);
        Var *r = make_var(NULL);
        emit(make_inst(&(Inst){ LOAD, r, m }));
        return r;
    }
    case AST_COMPOUND_STMT: {
        Vector *body = node->stmts;
        for (int i = 0; i < vec_len(body); i++)
            walk(vec_get(body, i));
        return NULL;
    }
    case AST_RETURN: {
        Var *v = walk(node->retval);
        emit(make_inst(&(Inst){ RET, v }));
        return NULL;
    }
    case AST_CONV:
        return walk(node->operand);
    case '+': {
        Var *dst = make_var(NULL);
        Var *lhs = walk(node->left);
        Var *rhs = walk(node->right);
        emit(make_inst(&(Inst){ ADD, dst, lhs, rhs }));
        return dst;
    }
    case AST_LITERAL:
        assert(node->ty->kind == KIND_INT);
        return make_literal(node->ival);
    default:
        error("unknown node: %s", a2s(node));
    }
}

static Func *translate(Vector *toplevels) {
    assert(vec_len(toplevels) == 1);
    Node *node = vec_head(toplevels);
    assert(node->kind == AST_FUNC);

    char *name = node->fname;
    walk(node->body);
    return make_func(&(Func){ name, insts });
}

static char *str(Var *v) {
    switch (v->kind) {
    case VAR:
        return format("%%%s", v->reg);
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
            write("movq %s, %s", str(in->arg2), str(in->arg1));
            write("addq %s, %s", str(in->arg3), str(in->arg1));
            break;
        case RET:
            write("movq %s, %%rax", str(in->arg1));
            write("jmp end");
            break;
        case LOAD: {
            Var *lhs = in->arg1;
            Mem *rhs = in->arg2;
            write("movq %d(%%rbp), %s", rhs->off, str(lhs));
            break;
        }
        case STORE: {
            Mem *lhs = in->arg1;
            Var *rhs = in->arg2;
            write("movq %s, %d(%%rbp)", str(rhs), lhs->off);
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
        v->reg = nextreg();
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
