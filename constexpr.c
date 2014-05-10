#include "8cc.h"


static void eval_constliteral(ConstExpr *cexpr, Node * node) {
    if (is_inttype(node->ctype)){
        cexpr->label = 0;
        cexpr->constant = node->ival;
        return;
    }
    error("Integer expression expected, but got %s", a2s(node));
}

#define TRIVIAL_UNOP(OP,NAME) \
    static void NAME(ConstExpr *cexpr, Node * node) {\
        eval_constexpr(cexpr, node->operand);\
        if(cexpr->label) {\
            error("Cannot apply " #OP " to address constant %s", a2s(node));\
        }\
        cexpr->constant = OP cexpr->constant;\
    }

TRIVIAL_UNOP(!, eval_constnot)
TRIVIAL_UNOP(~, eval_constbnot)
TRIVIAL_UNOP(-, eval_constuminus)
#undef TRIVIAL_UNOP

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

static void eval_constbinop(ConstExpr *left, Node * node) {
    ConstExpr right;
    eval_constexpr(left, node->left);
    eval_constexpr(&right, node->right);
    switch(node->type) {
        case '+': {
            int lmult = 1;
            int rmult = 1;
            if (left->label && right.label) {
                error("Cannot perform + on two address constants.");
            }
            if(node->ctype->type == CTYPE_PTR) {
                if (is_inttype(node->left->ctype)) {
                    lmult = node->ctype->ptr->size;
                }
                if (is_inttype(node->right->ctype)) {
                    rmult = node->ctype->ptr->size;
                }
            }
            if(right.label) {
                left->label = right.label;
            }
            left->constant = (left->constant * lmult) + (right.constant * rmult);
            return;
        }
        case OP_SHR:
            if(left->label || right.label) {
                error("Cannot perform >> on address constants.");
            }
            left->constant = ((unsigned long)left->constant) >> right.constant;
            return;
        #define TRIVIAL_CASE(OP, CHAR) \
            case CHAR: \
                if(left->label || right.label) {\
                     error("Cannot perform " #OP " on address constants.");\
                 }\
                 left->constant = left->constant OP right.constant;\
                 return

        TRIVIAL_CASE(-, '-');
        TRIVIAL_CASE(*, '*');
        TRIVIAL_CASE(/, '/');
        TRIVIAL_CASE(%, '%');
        TRIVIAL_CASE(<, '<');
        TRIVIAL_CASE(>, '>');
        TRIVIAL_CASE(^, '^');
        TRIVIAL_CASE(&, '&');
        TRIVIAL_CASE(|, '|');
        TRIVIAL_CASE(==, OP_EQ);
        TRIVIAL_CASE(!=, OP_NE);
        TRIVIAL_CASE(>=, OP_GE);
        TRIVIAL_CASE(<=, OP_LE);
        TRIVIAL_CASE(<<, OP_SAL);
        TRIVIAL_CASE(>>, OP_SAR);
        TRIVIAL_CASE(&&, OP_LOGAND);
        TRIVIAL_CASE(||, OP_LOGOR);
        default:
            error("Internal error.");
    }
    #undef TRIVIAL_CASE
}

static void eval_conststruct_ref(ConstExpr *cexpr, Node *node, int offset) {
    if (node->type == AST_STRUCT_REF)
        return eval_conststruct_ref(cexpr, node->struc, node->ctype->offset + offset);
    eval_constexpr(cexpr ,node);
    cexpr->constant += offset;
}

void eval_constexpr(ConstExpr *cexpr, Node *node) {
    switch (node->type) {
    case AST_LITERAL:
        eval_constliteral(cexpr, node);
        return;
    case AST_GVAR:
        cexpr->constant = 0;
        cexpr->label = node->varname;
        return;
    case '!':
        eval_constnot(cexpr, node);
        return;
    case '~':
        eval_constbnot(cexpr, node);
        return;
    case OP_UMINUS:
        eval_constuminus(cexpr, node);
        return;
    case OP_CAST:
        /* fallthrough */
    case AST_CONV:
        eval_constexpr(cexpr, node->operand);
        return;
    case AST_ADDR:
        if (node->operand->type == AST_STRUCT_REF) {
            eval_conststruct_ref(cexpr, node->operand, 0);
            return;
        }
        eval_constexpr(cexpr, node->operand);
        return;
    case AST_DEREF:
        if (node->operand->ctype->type == CTYPE_PTR) {
            eval_constexpr(cexpr, node->operand);
            return;
        }
        goto error;
    case AST_TERNARY:
        eval_constternary(cexpr,node);
        return;
    case '+':
    case '-':
    case '*':
    case '/':
    case '<':
    case '>':
    case '^':
    case '&':
    case '|':
    case '%':
    case OP_EQ:
    case OP_GE:
    case OP_LE:
    case OP_NE:
    case OP_SAL:
    case OP_SAR:
    case OP_SHR:
    case OP_LOGAND:
    case OP_LOGOR:
        eval_constbinop(cexpr,node);
        return;
    default:
    error:
        error("Constant expression expected, but got %s", a2s(node));
    }
}
