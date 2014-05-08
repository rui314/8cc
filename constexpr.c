#include "8cc.h"


static void eval_constliteral(ConstExpr *cexpr, Node * node) {
    if (is_inttype(node->ctype)){
        cexpr->label = 0;
        cexpr->constant = node->ival;
        return;
    }
    error("Integer expression expected, but got %s", a2s(node));
}

static void eval_constnot(ConstExpr *cexpr, Node * node) {
    eval_constexpr(cexpr, node->operand);
    if(cexpr->label) {
        error("Cannot apply ! to address constant %s", a2s(node));
    }
    cexpr->constant = !cexpr->constant;
}

static void eval_constbnot(ConstExpr *cexpr, Node * node) {
    eval_constexpr(cexpr, node->operand);
    if(cexpr->label) {
        error("Cannot apply ~ to address constant %s", a2s(node));
    }
    cexpr->constant = ~cexpr->constant;
}

static void eval_constuminus(ConstExpr *cexpr, Node * node) {
    eval_constexpr(cexpr, node->operand);
    if(cexpr->label) {
        error("Cannot apply unary - to address constant %s", a2s(node));
    }
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
           
            /* BROKEN
            if(node->ctype->type == CTYPE_PTR) {
                if (is_inttype(node->left->ctype)) {
                    //printf("lmult...\n");
                    lmult = node->ctype->ptr->size;
                }
                
                if (is_inttype(node->right->ctype)) {
                    //printf("rmult...\n");
                    rmult = node->ctype->ptr->size;
                }
            }
            */
            
            if(right.label) {
                left->label = right.label;
            }
            
            left->constant = (left->constant * lmult) + (right.constant * rmult);
            return;
        }
        case '-':
            if(left->label || right.label) {
                error("Cannot perform - on address constants.");
            }
            left->constant = left->constant - right.constant;
            return;
        case '*':
            if(left->label || right.label) {
                error("Cannot perform * on address constants.");
            }
            left->constant = left->constant * right.constant;
            return;
        case '/':
            if(left->label || right.label) {
                error("Cannot perform / on address constants.");
            }
            left->constant = left->constant / right.constant;
            return;
        case '<':
            if(left->label || right.label) {
                error("Cannot perform < on address constants.");
            }
            left->constant = left->constant < right.constant;
            return;
        case '>':
            if(left->label || right.label) {
                error("Cannot perform > on address constants.");
            }
            left->constant = left->constant > right.constant;
            return;
        case '^':
            if(left->label || right.label) {
                error("Cannot perform ^ on address constants.");
            }
            left->constant = left->constant ^ right.constant;
            return;
        case '&':
            if(left->label || right.label) {
                error("Cannot perform & on address constants.");
            }
            left->constant = left->constant & right.constant;
            return;
        case '|':
            if(left->label || right.label) {
                error("Cannot perform | on address constants.");
            }
            left->constant = left->constant | right.constant;
            return;
        case '%':
            if(left->label || right.label) {
                error("Cannot perform modulo on address constants.");
            }
            left->constant = left->constant % right.constant;
            return;
        case OP_EQ:
            if(left->label || right.label) {
                error("Cannot perform == on address constants.");
            }
            left->constant = left->constant == right.constant;
            return;
        case OP_GE:
            if(left->label || right.label) {
                error("Cannot perform >= on address constants.");
            }
            left->constant = left->constant >= right.constant;
            return;
        case OP_LE:
            if(left->label || right.label) {
                error("Cannot perform <= on address constants.");
            }
            left->constant = left->constant <= right.constant;
            return;
        case OP_NE:
            if(left->label || right.label) {
                error("Cannot perform != on address constants.");
            }
            left->constant = left->constant != right.constant;
            return;
        case OP_SAL:
            if(left->label || right.label) {
                error("Cannot perform >> on address constants.");
            }
            left->constant = left->constant << right.constant;
            return;
        case OP_SAR:
            if(left->label || right.label) {
                error("Cannot perform >> on address constants.");
            }
            left->constant = left->constant >> right.constant;
            return;
        case OP_SHR:
            if(left->label || right.label) {
                error("Cannot perform >> on address constants.");
            }
            left->constant = ((unsigned long)left->constant) >> right.constant;
            return;
        case OP_LOGAND:
            if(left->label || right.label) {
                error("Cannot perform && on address constants.");
            }
            left->constant = left->constant && right.constant;
            return;
        case OP_LOGOR:
            if(left->label || right.label) {
                error("Cannot perform || on address constants.");
            }
            left->constant = left->constant || right.constant;
            return;
        default:
            error("Internal error.");
    }
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

