#include "8cc.h"

char *ctype_to_string(Ctype *ctype) {
    if (!ctype)
        return "(nil)";
    switch (ctype->type) {
    case CTYPE_VOID: return "void";
    case CTYPE_CHAR: return "char";
    case CTYPE_INT:  return "int";
    case CTYPE_LONG: return "long";
    case CTYPE_FLOAT: return "float";
    case CTYPE_DOUBLE: return "double";
    case CTYPE_PTR: {
        String *s = make_string();
        string_appendf(s, "*%s", ctype_to_string(ctype->ptr));
        return get_cstring(s);
    }
    case CTYPE_ARRAY: {
        String *s = make_string();
        string_appendf(s, "[%d]%s", ctype->len, ctype_to_string(ctype->ptr));
        return get_cstring(s);
    }
    case CTYPE_STRUCT: {
        String *s = make_string();
        string_appendf(s, "(struct");
        for (Iter *i = list_iter(dict_values(ctype->fields)); !iter_end(i);)
            string_appendf(s, " (%s)", ctype_to_string(iter_next(i)));
        string_appendf(s, ")");
        return get_cstring(s);
    }
    case CTYPE_FUNC: {
        String *s = make_string();
        string_appendf(s, "%s(", ctype_to_string(ctype->rettype));
        for (Iter *i = list_iter(ctype->params); !iter_end(i);) {
            Ctype *t = iter_next(i);
            string_appendf(s, "%s", ctype_to_string(t));
            if (!iter_end(i))
                string_append(s, ',');
        }
        string_append(s, ')');
        return get_cstring(s);
    }
    default: error("Unknown ctype: %d", ctype);
    }
}

static void uop_to_string(String *buf, char *op, Ast *ast) {
    string_appendf(buf, "(%s %s)", op, a2s(ast->operand));
}

static void binop_to_string(String *buf, char *op, Ast *ast) {
    string_appendf(buf, "(%s %s %s)",
                   op, a2s(ast->left), a2s(ast->right));
}

static void a2s_int(String *buf, Ast *ast) {
    if (!ast) {
        string_appendf(buf, "(nil)");
        return;
    }
    switch (ast->type) {
    case AST_LITERAL:
        switch (ast->ctype->type) {
        case CTYPE_CHAR:
            if (ast->ival == '\n')
                string_appendf(buf, "'\n'");
            else if (ast->ival == '\\')
                string_appendf(buf, "'\\\\'");
            else
                string_appendf(buf, "'%c'", ast->ival);
            break;
        case CTYPE_INT:
            string_appendf(buf, "%d", ast->ival);
            break;
        case CTYPE_LONG:
            string_appendf(buf, "%ldL", ast->ival);
            break;
        case CTYPE_FLOAT:
        case CTYPE_DOUBLE:
            string_appendf(buf, "%f", ast->fval);
            break;
        default:
            error("internal error");
        }
        break;
    case AST_STRING:
        string_appendf(buf, "\"%s\"", quote_cstring(ast->sval));
        break;
    case AST_LVAR:
    case AST_GVAR:
        string_appendf(buf, "%s", ast->varname);
        break;
    case AST_FUNCALL: {
        string_appendf(buf, "(%s)%s(", ctype_to_string(ast->ctype), ast->fname);
        for (Iter *i = list_iter(ast->args); !iter_end(i);) {
            string_appendf(buf, "%s", a2s(iter_next(i)));
            if (!iter_end(i))
                string_appendf(buf, ",");
        }
        string_appendf(buf, ")");
        break;
    }
    case AST_FUNC: {
        string_appendf(buf, "(%s)%s(", ctype_to_string(ast->ctype), ast->fname);
        for (Iter *i = list_iter(ast->params); !iter_end(i);) {
            Ast *param = iter_next(i);
            string_appendf(buf, "%s %s", ctype_to_string(param->ctype), a2s(param));
            if (!iter_end(i))
                string_appendf(buf, ",");
        }
        string_appendf(buf, ")");
        a2s_int(buf, ast->body);
        break;
    }
    case AST_DECL:
        string_appendf(buf, "(decl %s %s",
                       ctype_to_string(ast->declvar->ctype),
                       ast->declvar->varname);
        if (ast->declinit)
            string_appendf(buf, " %s)", a2s(ast->declinit));
        else
            string_appendf(buf, ")");
        break;
    case AST_ARRAY_INIT:
        string_appendf(buf, "{");
        for (Iter *i = list_iter(ast->arrayinit); !iter_end(i);) {
            a2s_int(buf, iter_next(i));
            if (!iter_end(i))
                string_appendf(buf, ",");
        }
        string_appendf(buf, "}");
        break;
    case AST_IF:
        string_appendf(buf, "(if %s %s",
                       a2s(ast->cond),
                       a2s(ast->then));
        if (ast->els)
            string_appendf(buf, " %s", a2s(ast->els));
        string_appendf(buf, ")");
        break;
    case AST_TERNARY:
        string_appendf(buf, "(? %s %s %s)",
                       a2s(ast->cond),
                       a2s(ast->then),
                       a2s(ast->els));
        break;
    case AST_FOR:
        string_appendf(buf, "(for %s %s %s ",
                       a2s(ast->forinit),
                       a2s(ast->forcond),
                       a2s(ast->forstep));
        string_appendf(buf, "%s)", a2s(ast->forbody));
        break;
    case AST_RETURN:
        string_appendf(buf, "(return %s)", a2s(ast->retval));
        break;
    case AST_COMPOUND_STMT: {
        string_appendf(buf, "{");
        for (Iter *i = list_iter(ast->stmts); !iter_end(i);) {
            a2s_int(buf, iter_next(i));
            string_appendf(buf, ";");
        }
        string_appendf(buf, "}");
        break;
    }
    case AST_STRUCT_REF:
        a2s_int(buf, ast->struc);
        string_appendf(buf, ".");
        string_appendf(buf, ast->field);
        break;
    case AST_ADDR:  uop_to_string(buf, "addr", ast); break;
    case AST_DEREF: uop_to_string(buf, "deref", ast); break;
    case PUNCT_INC: uop_to_string(buf, "++", ast); break;
    case PUNCT_DEC: uop_to_string(buf, "--", ast); break;
    case PUNCT_LOGAND: binop_to_string(buf, "and", ast); break;
    case PUNCT_LOGOR:  binop_to_string(buf, "or", ast); break;
    case '!': uop_to_string(buf, "!", ast); break;
    case '&': binop_to_string(buf, "&", ast); break;
    case '|': binop_to_string(buf, "|", ast); break;
    default: {
        char *left = a2s(ast->left);
        char *right = a2s(ast->right);
        if (ast->type == PUNCT_EQ)
            string_appendf(buf, "(== ");
        else
            string_appendf(buf, "(%c ", ast->type);
        string_appendf(buf, "%s %s)", left, right);
    }
    }
}

char *a2s(Ast *ast) {
    String *s = make_string();
    a2s_int(s, ast);
    return get_cstring(s);
}

char *t2s(Token *tok) {
    if (!tok)
        return "(null)";
    String *s = make_string();
    switch (tok->type) {
    case TTYPE_IDENT:
        return tok->sval;
    case TTYPE_PUNCT:
        if (is_punct(tok, PUNCT_EQ))
            string_appendf(s, "==");
        else
            string_appendf(s, "%c", tok->c);
        return get_cstring(s);
    case TTYPE_CHAR: {
        string_append(s, tok->c);
        return get_cstring(s);
    }
    case TTYPE_NUMBER:
        return tok->sval;
    case TTYPE_STRING:
        string_appendf(s, "\"%s\"", tok->sval);
        return get_cstring(s);
    case TTYPE_NEWLINE:
        return "(newline)";
    }
    error("internal error: unknown token type: %d", tok->type);
}
