// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#include "8cc.h"

static char *maybe_add_bitfield(char *name, Ctype *ctype) {
    if (ctype->bitsize > 0)
        return format("%s:%d:%d", name, ctype->bitoff, ctype->bitoff + ctype->bitsize);
    return name;
}

static char *c2s_int(Dict *dict, Ctype *ctype) {
    if (!ctype)
        return "(nil)";
    switch (ctype->type) {
    case CTYPE_VOID: return "void";
    case CTYPE_BOOL: return "_Bool";
    case CTYPE_CHAR: return maybe_add_bitfield("char", ctype);
    case CTYPE_SHORT: return maybe_add_bitfield("short", ctype);
    case CTYPE_INT:  return maybe_add_bitfield("int", ctype);
    case CTYPE_LONG: return maybe_add_bitfield("long", ctype);
    case CTYPE_LLONG: return maybe_add_bitfield("long long", ctype);
    case CTYPE_FLOAT: return "float";
    case CTYPE_DOUBLE: return "double";
    case CTYPE_LDOUBLE: return "long double";
    case CTYPE_PTR:
        return format("*%s", c2s_int(dict, ctype->ptr));
    case CTYPE_ARRAY:
        return format("[%d]%s", ctype->len, c2s_int(dict, ctype->ptr));
    case CTYPE_STRUCT: {
        char *type = ctype->is_struct ? "struct" : "union";
        if (dict_get(dict, format("%p", ctype)))
            return format("(%s)", type);
        dict_put(dict, format("%p", ctype), (void *)1);
        String *s = make_string();
        string_appendf(s, "(%s", type);
        for (Iter *i = list_iter(dict_values(ctype->fields)); !iter_end(i);) {
            Ctype *fieldtype = iter_next(i);
            string_appendf(s, " (%s)", c2s_int(dict, fieldtype));
        }
        string_appendf(s, ")");
        return get_cstring(s);
    }
    case CTYPE_FUNC: {
        String *s = make_string();
        string_appendf(s, "(");
        for (Iter *i = list_iter(ctype->params); !iter_end(i);) {
            Ctype *t = iter_next(i);
            string_appendf(s, "%s", c2s_int(dict, t));
            if (!iter_end(i))
                string_append(s, ',');
        }
        string_appendf(s, ")=>%s", c2s_int(dict, ctype->rettype));
        return get_cstring(s);
    }
    default:
        return format("(Unknown ctype: %d)", ctype->type);
    }
}

char *c2s(Ctype *ctype) {
    return c2s_int(make_dict(NULL), ctype);
}

static void uop_to_string(String *buf, char *op, Node *node) {
    string_appendf(buf, "(%s %s)", op, a2s(node->operand));
}

static void binop_to_string(String *buf, char *op, Node *node) {
    string_appendf(buf, "(%s %s %s)",
                   op, a2s(node->left), a2s(node->right));
}

static void a2s_declinit(String *buf, List *initlist) {
    for (Iter *i = list_iter(initlist); !iter_end(i);) {
        Node *init = iter_next(i);
        string_appendf(buf, "%s", a2s(init));
        if (!iter_end(i))
            string_appendf(buf, " ");
    }
}

static void a2s_int(String *buf, Node *node) {
    if (!node) {
        string_appendf(buf, "(nil)");
        return;
    }
    switch (node->type) {
    case AST_LITERAL:
        switch (node->ctype->type) {
        case CTYPE_CHAR:
            if (node->ival == '\n')      string_appendf(buf, "'\n'");
            else if (node->ival == '\\') string_appendf(buf, "'\\\\'");
            else if (node->ival == '\0') string_appendf(buf, "'\\0'");
            else string_appendf(buf, "'%c'", node->ival);
            break;
        case CTYPE_INT:
            string_appendf(buf, "%d", node->ival);
            break;
        case CTYPE_LONG:
            string_appendf(buf, "%ldL", node->ival);
            break;
        case CTYPE_FLOAT:
        case CTYPE_DOUBLE:
            string_appendf(buf, "%f", node->fval);
            break;
        default:
            error("internal error");
        }
        break;
    case AST_STRING:
        string_appendf(buf, "\"%s\"", quote_cstring(node->sval));
        break;
    case AST_LVAR:
        string_appendf(buf, "lv=%s", node->varname);
        if (node->lvarinit) {
            string_appendf(buf, "(");
            a2s_declinit(buf, node->lvarinit);
            string_appendf(buf, ")");
        }
        break;
    case AST_GVAR:
        string_appendf(buf, "gv=%s", node->varname);
        break;
    case AST_FUNCALL:
    case AST_FUNCPTR_CALL: {
        string_appendf(buf, "(%s)%s(", c2s(node->ctype),
                       node->type == AST_FUNCALL ? node->fname : a2s(node));
        for (Iter *i = list_iter(node->args); !iter_end(i);) {
            string_appendf(buf, "%s", a2s(iter_next(i)));
            if (!iter_end(i))
                string_appendf(buf, ",");
        }
        string_appendf(buf, ")");
        break;
    }
    case AST_FUNCDESG: {
        string_appendf(buf, "(funcdesg %s)", a2s(node->fptr));
        break;
    }
    case AST_FUNC: {
        string_appendf(buf, "(%s)%s(", c2s(node->ctype), node->fname);
        for (Iter *i = list_iter(node->params); !iter_end(i);) {
            Node *param = iter_next(i);
            string_appendf(buf, "%s %s", c2s(param->ctype), a2s(param));
            if (!iter_end(i))
                string_appendf(buf, ",");
        }
        string_appendf(buf, ")");
        a2s_int(buf, node->body);
        break;
    }
    case AST_DECL:
        string_appendf(buf, "(decl %s %s",
                       c2s(node->declvar->ctype),
                       node->declvar->varname);
        if (node->declinit) {
            string_appendf(buf, " ");
            a2s_declinit(buf, node->declinit);
        }
        string_appendf(buf, ")");
        break;
    case AST_INIT:
        string_appendf(buf, "%s@%d", a2s(node->initval), node->initoff, c2s(node->totype));
        break;
    case AST_CONV:
        string_appendf(buf, "(conv %s=>%s)", a2s(node->operand), c2s(node->ctype));
        break;
    case AST_IF:
        string_appendf(buf, "(if %s %s",
                       a2s(node->cond),
                       a2s(node->then));
        if (node->els)
            string_appendf(buf, " %s", a2s(node->els));
        string_appendf(buf, ")");
        break;
    case AST_TERNARY:
        string_appendf(buf, "(? %s %s %s)",
                       a2s(node->cond),
                       a2s(node->then),
                       a2s(node->els));
        break;
    case AST_FOR:
        string_appendf(buf, "(for %s %s %s %s)",
                       a2s(node->forinit),
                       a2s(node->forcond),
                       a2s(node->forstep),
                       a2s(node->forbody));
        break;
    case AST_WHILE:
        string_appendf(buf, "(while %s %s)",
                       a2s(node->forcond),
                       a2s(node->forbody));
        break;
    case AST_DO:
        string_appendf(buf, "(do %s %s)",
                       a2s(node->forcond),
                       a2s(node->forbody));
        break;
    case AST_RETURN:
        string_appendf(buf, "(return %s)", a2s(node->retval));
        break;
    case AST_COMPOUND_STMT: {
        string_appendf(buf, "{");
        for (Iter *i = list_iter(node->stmts); !iter_end(i);) {
            a2s_int(buf, iter_next(i));
            string_appendf(buf, ";");
        }
        string_appendf(buf, "}");
        break;
    }
    case AST_STRUCT_REF:
        a2s_int(buf, node->struc);
        string_appendf(buf, ".");
        string_appendf(buf, node->field);
        break;
    case AST_ADDR:  uop_to_string(buf, "addr", node); break;
    case AST_DEREF: uop_to_string(buf, "deref", node); break;
    case OP_UMINUS: uop_to_string(buf, "-", node); break;
    case OP_SAL:  binop_to_string(buf, "<<", node); break;
    case OP_SAR:
    case OP_SHR:  binop_to_string(buf, ">>", node); break;
    case OP_GE:  binop_to_string(buf, ">=", node); break;
    case OP_LE:  binop_to_string(buf, "<=", node); break;
    case OP_NE:  binop_to_string(buf, "!=", node); break;
    case OP_PRE_INC: uop_to_string(buf, "pre++", node); break;
    case OP_PRE_DEC: uop_to_string(buf, "pre--", node); break;
    case OP_POST_INC: uop_to_string(buf, "post++", node); break;
    case OP_POST_DEC: uop_to_string(buf, "post--", node); break;
    case OP_LOGAND: binop_to_string(buf, "and", node); break;
    case OP_LOGOR:  binop_to_string(buf, "or", node); break;
    case OP_A_ADD:  binop_to_string(buf, "+=", node); break;
    case OP_A_SUB:  binop_to_string(buf, "-=", node); break;
    case OP_A_MUL:  binop_to_string(buf, "*=", node); break;
    case OP_A_DIV:  binop_to_string(buf, "/=", node); break;
    case OP_A_MOD:  binop_to_string(buf, "%=", node); break;
    case OP_A_AND:  binop_to_string(buf, "&=", node); break;
    case OP_A_OR:   binop_to_string(buf, "|=", node); break;
    case OP_A_XOR:  binop_to_string(buf, "^=", node); break;
    case OP_A_SAL:  binop_to_string(buf, "<<=", node); break;
    case OP_A_SAR:
    case OP_A_SHR:  binop_to_string(buf, ">>=", node); break;
    case '!': uop_to_string(buf, "!", node); break;
    case '&': binop_to_string(buf, "&", node); break;
    case '|': binop_to_string(buf, "|", node); break;
    case OP_CAST: {
        string_appendf(buf, "((%s)=>(%s) %s)",
                       c2s(node->operand->ctype),
                       c2s(node->ctype),
                       a2s(node->operand));
        break;
    }
    case OP_LABEL_ADDR:
        string_appendf(buf, "&&%s", node->label);
        break;
    default: {
        char *left = a2s(node->left);
        char *right = a2s(node->right);
        if (node->type == OP_EQ)
            string_appendf(buf, "(== ");
        else
            string_appendf(buf, "(%c ", node->type);
        string_appendf(buf, "%s %s)", left, right);
    }
    }
}

char *a2s(Node *node) {
    String *s = make_string();
    a2s_int(s, node);
    return get_cstring(s);
}

char *t2s(Token *tok) {
    if (!tok)
        return "(null)";
    switch (tok->type) {
    case TIDENT:
        return tok->sval;
    case TPUNCT:
        switch (tok->punct) {
#define punct(ident, str)                       \
            case ident: return str;
#define keyword(ident, str, _)                  \
            case ident: return str;
#include "keyword.h"
#undef keyword
#undef punct
        default: return format("%c", tok->c);
        }
    case TCHAR:
        return quote_char(tok->c);
    case TNUMBER:
        return tok->sval;
    case TSTRING:
        return format("\"%s\"", quote_cstring(tok->sval));
    case TNEWLINE:
        return "(newline)";
    case TSPACE:
        return "(space)";
    case TMACRO_PARAM:
        return "(macro-param)";
    }
    error("internal error: unknown token type: %d", tok->type);
}
