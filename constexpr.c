#include "8cc.h"

int is_integer_constant(ConstExpr *cexpr) {
    return cexpr->label ? 0 : 1;
}

static void eval_constliteral(ConstExpr *cexpr, Node * node) {
    if (is_inttype(node->ctype)){
        cexpr->constant = node->ival;
        return;
    }
    error("Integer expression expected, but got %s", a2s(node));
}

static void eval_constnot(ConstExpr *cexpr, Node * node) {
    if(cexpr->label) {
        error("Cannot apply ! to address constant %s", a2s(node));
    }
    eval_constexpr(cexpr, node->operand);
    cexpr->constant = !cexpr->constant;
}

static void eval_constbnot(ConstExpr *cexpr, Node * node) {
    if(cexpr->label) {
        error("Cannot apply ~ to address constant %s", a2s(node));
    }
    eval_constexpr(cexpr, node->operand);
    cexpr->constant = ~cexpr->constant;
}

static void eval_constuminus(ConstExpr *cexpr, Node * node) {
    if(cexpr->label) {
        error("Cannot apply unary - to address constant %s", a2s(node));
    }
    eval_constexpr(cexpr, node->operand);
    cexpr->constant = -cexpr->constant;
}

static void eval_constternary(ConstExpr *cexpr, Node * node) {
    eval_constexpr(cexpr, node->cond);
    if (cexpr->label || cexpr->constant) {
        if(node->then){
            eval_constexpr(cexpr, node->then);
        }
    } else {
        eval_constexpr(cexpr, node->els);
    }
}

void eval_constexpr(ConstExpr *cexpr, Node *node) {
    ConstExpr otherexpr;
    switch (node->type) {
    case AST_LITERAL:
        eval_constliteral(cexpr, node);
        return;
    case '!':
        eval_constnot(cexpr, node->operand);
        return;
    case '~':
        eval_constbnot(cexpr, node->operand);
        return;
    case OP_UMINUS:
        eval_constuminus(cexpr, node->operand);
        return;
    case OP_CAST:
        /* fallthrough */
    case AST_CONV:
        return eval_constexpr(cexpr, node->operand);
    case AST_ADDR:
        //if (node->operand->type == AST_STRUCT_REF)
        //    return eval_struct_ref(node->operand, 0);
        goto error;
    case AST_DEREF:
        //if (node->operand->ctype->type == CTYPE_PTR)
        //    return eval_intexpr(node->operand);
        goto error;
    case AST_TERNARY:
        eval_constternary(cexpr,node);
/*
#define L (eval_intexpr(node->left))
#define R (eval_intexpr(node->right))
    case '+': return L + R;
    case '-': return L - R;
    case '*': return L * R;
    case '/': return L / R;
    case '<': return L < R;
    case '>': return L > R;
    case '^': return L ^ R;
    case '&': return L & R;
    case '|': return L | R;
    case '%': return L % R;
    case OP_EQ: return L == R;
    case OP_GE: return L >= R;
    case OP_LE: return L <= R;
    case OP_NE: return L != R;
    case OP_SAL: return L << R;
    case OP_SAR: return L >> R;
    case OP_SHR: return ((unsigned long)L) >> R;
    case OP_LOGAND: return L && R;
    case OP_LOGOR:  return L || R;
#undef L
#undef R
*/
    default:
    error:
        error("Constant expression expected, but got %s", a2s(node));
    }
}

