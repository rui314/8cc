#include "8cc.h"

static char *c2s_int(Dict *dict, Ctype *ctype) {
    if (!ctype)
        return "(nil)";
    switch (ctype->type) {
    case CTYPE_VOID: return "void";
    case CTYPE_CHAR: return "char";
    case CTYPE_SHORT: return "short";
    case CTYPE_INT:  return "int";
    case CTYPE_LONG: return "long";
    case CTYPE_LLONG: return "long long";
    case CTYPE_FLOAT: return "float";
    case CTYPE_DOUBLE: return "double";
    case CTYPE_LDOUBLE: return "long double";
    case CTYPE_PTR:
        return format("*%s", c2s_int(dict, ctype->ptr));
    case CTYPE_ARRAY:
        return format("[%d]%s", ctype->len, c2s_int(dict, ctype->ptr));
    case CTYPE_STRUCT: {
        if (dict_get(dict, format("%p", ctype)))
            return "(struct)";
        dict_put(dict, format("%p", ctype), (void *)1);
        String *s = make_string();
        string_appendf(s, "(struct");
        for (Iter *i = list_iter(dict_values(ctype->fields)); !iter_end(i);)
            string_appendf(s, " (%s)", c2s_int(dict, iter_next(i)));
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
        string_appendf(s, ") -> %s", c2s_int(dict, ctype->rettype));
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
    case AST_GVAR:
        string_appendf(buf, "%s", node->varname);
        break;
    case AST_FUNCALL: {
        string_appendf(buf, "(%s)%s(", c2s(node->ctype), node->fname);
        for (Iter *i = list_iter(node->args); !iter_end(i);) {
            string_appendf(buf, "%s", a2s(iter_next(i)));
            if (!iter_end(i))
                string_appendf(buf, ",");
        }
        string_appendf(buf, ")");
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
        if (node->declinit)
            string_appendf(buf, " %s)", a2s(node->declinit));
        else
            string_appendf(buf, ")");
        break;
    case AST_INIT_LIST:
        string_appendf(buf, "{");
        for (Iter *i = list_iter(node->initlist); !iter_end(i);) {
            a2s_int(buf, iter_next(i));
            if (!iter_end(i))
                string_appendf(buf, ",");
        }
        string_appendf(buf, "}");
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
        string_appendf(buf, "(for %s %s %s ",
                       a2s(node->forinit),
                       a2s(node->forcond),
                       a2s(node->forstep));
        string_appendf(buf, "%s)", a2s(node->forbody));
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
    case OP_GE:  binop_to_string(buf, ">=", node); break;
    case OP_LE:  binop_to_string(buf, "<=", node); break;
    case OP_NE:  binop_to_string(buf, "!=", node); break;
    case OP_INC: uop_to_string(buf, "++", node); break;
    case OP_DEC: uop_to_string(buf, "--", node); break;
    case OP_LOGAND: binop_to_string(buf, "and", node); break;
    case OP_LOGOR:  binop_to_string(buf, "or", node); break;
    case '!': uop_to_string(buf, "!", node); break;
    case '&': binop_to_string(buf, "&", node); break;
    case '|': binop_to_string(buf, "|", node); break;
    case OP_CAST: {
        string_appendf(buf, "((%s) -> (%s) %s)",
                       c2s(node->operand->ctype),
                       c2s(node->ctype),
                       a2s(node->operand));
        break;
    }
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
    case TTYPE_IDENT:
        return tok->sval;
    case TTYPE_PUNCT:
        if (is_punct(tok, OP_EQ))
            return "==";
        return format("%c", tok->c);
    case TTYPE_CHAR:
        return format("%c", tok->c);
    case TTYPE_NUMBER:
        return tok->sval;
    case TTYPE_STRING:
        return format("\"%s\"", tok->sval);
    case TTYPE_NEWLINE:
        return "(newline)";
    case TTYPE_SPACE:
        return "(space)";
    case TTYPE_MACRO_PARAM:
        return "(macro-param)";
    }
    error("internal error: unknown token type: %d", tok->type);
}
