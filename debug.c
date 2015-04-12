// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include "8cc.h"

static char *decorate_int(char *name, Type *ty) {
    char *u = (ty->usig) ? "u" : "";
    if (ty->bitsize > 0)
        return format("%s%s:%d:%d", u, name, ty->bitoff, ty->bitoff + ty->bitsize);
    return format("%s%s", u, name);
}

static char *do_ty2s(Dict *dict, Type *ty) {
    if (!ty)
        return "(nil)";
    switch (ty->kind) {
    case KIND_VOID: return "void";
    case KIND_BOOL: return "_Bool";
    case KIND_CHAR: return decorate_int("char", ty);
    case KIND_SHORT: return decorate_int("short", ty);
    case KIND_INT:  return decorate_int("int", ty);
    case KIND_LONG: return decorate_int("long", ty);
    case KIND_LLONG: return decorate_int("llong", ty);
    case KIND_FLOAT: return "float";
    case KIND_DOUBLE: return "double";
    case KIND_LDOUBLE: return "long double";
    case KIND_PTR:
        return format("*%s", do_ty2s(dict, ty->ptr));
    case KIND_ARRAY:
        return format("[%d]%s", ty->len, do_ty2s(dict, ty->ptr));
    case KIND_STRUCT: {
        char *kind = ty->is_struct ? "struct" : "union";
        if (dict_get(dict, format("%p", ty)))
            return format("(%s)", kind);
        dict_put(dict, format("%p", ty), (void *)1);
        if (ty->fields) {
            Buffer *b = make_buffer();
            buf_printf(b, "(%s", kind);
            Vector *keys = dict_keys(ty->fields);
            for (int i = 0; i < vec_len(keys); i++) {
                char *key = vec_get(keys, i);
                Type *fieldtype = dict_get(ty->fields, key);
                buf_printf(b, " (%s)", do_ty2s(dict, fieldtype));
            }
            buf_printf(b, ")");
            return buf_body(b);
        }
    }
    case KIND_FUNC: {
        Buffer *b = make_buffer();
        buf_printf(b, "(");
        if (ty->params) {
            for (int i = 0; i < vec_len(ty->params); i++) {
                if (i > 0)
                    buf_printf(b, ",");
                Type *t = vec_get(ty->params, i);
                buf_printf(b, "%s", do_ty2s(dict, t));
            }
        }
        buf_printf(b, ")=>%s", do_ty2s(dict, ty->rettype));
        return buf_body(b);
    }
    default:
        return format("(Unknown ty: %d)", ty->kind);
    }
}

char *ty2s(Type *ty) {
    return do_ty2s(make_dict(), ty);
}

static void uop_to_string(Buffer *b, char *op, Node *node) {
    buf_printf(b, "(%s %s)", op, node2s(node->operand));
}

static void binop_to_string(Buffer *b, char *op, Node *node) {
    buf_printf(b, "(%s %s %s)", op, node2s(node->left), node2s(node->right));
}

static void a2s_declinit(Buffer *b, Vector *initlist) {
    for (int i = 0; i < vec_len(initlist); i++) {
        if (i > 0)
            buf_printf(b, " ");
        Node *init = vec_get(initlist, i);
        buf_printf(b, "%s", node2s(init));
    }
}

static void do_node2s(Buffer *b, Node *node) {
    if (!node) {
        buf_printf(b, "(nil)");
        return;
    }
    switch (node->kind) {
    case AST_LITERAL:
        switch (node->ty->kind) {
        case KIND_CHAR:
            if (node->ival == '\n')      buf_printf(b, "'\n'");
            else if (node->ival == '\\') buf_printf(b, "'\\\\'");
            else if (node->ival == '\0') buf_printf(b, "'\\0'");
            else buf_printf(b, "'%c'", node->ival);
            break;
        case KIND_INT:
            buf_printf(b, "%d", node->ival);
            break;
        case KIND_LONG:
            buf_printf(b, "%ldL", node->ival);
            break;
        case KIND_LLONG:
            buf_printf(b, "%lldL", node->ival);
            break;
        case KIND_FLOAT:
        case KIND_DOUBLE:
        case KIND_LDOUBLE:
            buf_printf(b, "%f", node->fval);
            break;
        case KIND_ARRAY:
            buf_printf(b, "\"%s\"", quote_cstring(node->sval));
            break;
        default:
            error("internal error");
        }
        break;
    case AST_LABEL:
        buf_printf(b, "%s:", node->label);
        break;
    case AST_LVAR:
        buf_printf(b, "lv=%s", node->varname);
        if (node->lvarinit) {
            buf_printf(b, "(");
            a2s_declinit(b, node->lvarinit);
            buf_printf(b, ")");
        }
        break;
    case AST_GVAR:
        buf_printf(b, "gv=%s", node->varname);
        break;
    case AST_FUNCALL:
    case AST_FUNCPTR_CALL: {
        buf_printf(b, "(%s)%s(", ty2s(node->ty),
                   node->kind == AST_FUNCALL ? node->fname : node2s(node));
        for (int i = 0; i < vec_len(node->args); i++) {
            if (i > 0)
                buf_printf(b, ",");
            buf_printf(b, "%s", node2s(vec_get(node->args, i)));
        }
        buf_printf(b, ")");
        break;
    }
    case AST_FUNCDESG: {
        buf_printf(b, "(funcdesg %s)", node->fname);
        break;
    }
    case AST_FUNC: {
        buf_printf(b, "(%s)%s(", ty2s(node->ty), node->fname);
        for (int i = 0; i < vec_len(node->params); i++) {
            if (i > 0)
                buf_printf(b, ",");
            Node *param = vec_get(node->params, i);
            buf_printf(b, "%s %s", ty2s(param->ty), node2s(param));
        }
        buf_printf(b, ")");
        do_node2s(b, node->body);
        break;
    }
    case AST_GOTO:
        buf_printf(b, "goto(%s)", node->label);
        break;
    case AST_DECL:
        buf_printf(b, "(decl %s %s",
                   ty2s(node->declvar->ty),
                   node->declvar->varname);
        if (node->declinit) {
            buf_printf(b, " ");
            a2s_declinit(b, node->declinit);
        }
        buf_printf(b, ")");
        break;
    case AST_INIT:
        buf_printf(b, "%s@%d", node2s(node->initval), node->initoff, ty2s(node->totype));
        break;
    case AST_CONV:
        buf_printf(b, "(conv %s=>%s)", node2s(node->operand), ty2s(node->ty));
        break;
    case AST_IF:
        buf_printf(b, "(if %s %s",
                   node2s(node->cond),
                   node2s(node->then));
        if (node->els)
            buf_printf(b, " %s", node2s(node->els));
        buf_printf(b, ")");
        break;
    case AST_TERNARY:
        buf_printf(b, "(? %s %s %s)",
                   node2s(node->cond),
                   node2s(node->then),
                   node2s(node->els));
        break;
    case AST_RETURN:
        buf_printf(b, "(return %s)", node2s(node->retval));
        break;
    case AST_COMPOUND_STMT: {
        buf_printf(b, "{");
        for (int i = 0; i < vec_len(node->stmts); i++) {
            do_node2s(b, vec_get(node->stmts, i));
            buf_printf(b, ";");
        }
        buf_printf(b, "}");
        break;
    }
    case AST_STRUCT_REF:
        do_node2s(b, node->struc);
        buf_printf(b, ".");
        buf_printf(b, node->field);
        break;
    case AST_ADDR:  uop_to_string(b, "addr", node); break;
    case AST_DEREF: uop_to_string(b, "deref", node); break;
    case OP_SAL:  binop_to_string(b, "<<", node); break;
    case OP_SAR:
    case OP_SHR:  binop_to_string(b, ">>", node); break;
    case OP_GE:  binop_to_string(b, ">=", node); break;
    case OP_LE:  binop_to_string(b, "<=", node); break;
    case OP_NE:  binop_to_string(b, "!=", node); break;
    case OP_PRE_INC: uop_to_string(b, "pre++", node); break;
    case OP_PRE_DEC: uop_to_string(b, "pre--", node); break;
    case OP_POST_INC: uop_to_string(b, "post++", node); break;
    case OP_POST_DEC: uop_to_string(b, "post--", node); break;
    case OP_LOGAND: binop_to_string(b, "and", node); break;
    case OP_LOGOR:  binop_to_string(b, "or", node); break;
    case OP_A_ADD:  binop_to_string(b, "+=", node); break;
    case OP_A_SUB:  binop_to_string(b, "-=", node); break;
    case OP_A_MUL:  binop_to_string(b, "*=", node); break;
    case OP_A_DIV:  binop_to_string(b, "/=", node); break;
    case OP_A_MOD:  binop_to_string(b, "%=", node); break;
    case OP_A_AND:  binop_to_string(b, "&=", node); break;
    case OP_A_OR:   binop_to_string(b, "|=", node); break;
    case OP_A_XOR:  binop_to_string(b, "^=", node); break;
    case OP_A_SAL:  binop_to_string(b, "<<=", node); break;
    case OP_A_SAR:
    case OP_A_SHR:  binop_to_string(b, ">>=", node); break;
    case '!': uop_to_string(b, "!", node); break;
    case '&': binop_to_string(b, "&", node); break;
    case '|': binop_to_string(b, "|", node); break;
    case OP_CAST: {
        buf_printf(b, "((%s)=>(%s) %s)",
                   ty2s(node->operand->ty),
                   ty2s(node->ty),
                   node2s(node->operand));
        break;
    }
    case OP_LABEL_ADDR:
        buf_printf(b, "&&%s", node->label);
        break;
    default: {
        char *left = node2s(node->left);
        char *right = node2s(node->right);
        if (node->kind == OP_EQ)
            buf_printf(b, "(== ");
        else
            buf_printf(b, "(%c ", node->kind);
        buf_printf(b, "%s %s)", left, right);
    }
    }
}

char *node2s(Node *node) {
    Buffer *b = make_buffer();
    do_node2s(b, node);
    return buf_body(b);
}

static char *encoding_prefix(int enc) {
    switch (enc) {
    case ENC_CHAR16: return "u";
    case ENC_CHAR32: return "U";
    case ENC_UTF8:   return "u8";
    case ENC_WCHAR:  return "L";
    }
    return "";
}

char *tok2s(Token *tok) {
    if (!tok)
        return "(null)";
    switch (tok->kind) {
    case TIDENT:
        return tok->sval;
    case TKEYWORD:
        switch (tok->id) {
#define op(id, str)         case id: return str;
#define keyword(id, str, _) case id: return str;
#include "keyword.inc"
#undef keyword
#undef op
        default: return format("%c", tok->id);
        }
    case TCHAR:
        return format("%s'%s'",
                      encoding_prefix(tok->enc),
                      quote_char(tok->c));
    case TNUMBER:
        return tok->sval;
    case TSTRING:
        return format("%s\"%s\"",
                      encoding_prefix(tok->enc),
                      quote_cstring(tok->sval));
    case TEOF:
        return "(eof)";
    case TINVALID:
        return format("%c", tok->c);
    case TNEWLINE:
        return "(newline)";
    case TSPACE:
        return "(space)";
    case TMACRO_PARAM:
        return "(macro-param)";
    }
    error("internal error: unknown token kind: %d", tok->kind);
}
