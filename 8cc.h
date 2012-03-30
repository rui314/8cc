#ifndef EIGHTCC_H
#define EIGHTCC_H

#include <stdbool.h>
#include <stdio.h>
#include "dict.h"
#include "list.h"
#include "util.h"

typedef struct {
    char *body;
    int nalloc;
    int len;
} String;

extern bool suppress_warning;

extern String *make_string(void);
extern char *format(char *fmt, ...);
extern char *get_cstring(String *s);
extern int string_len(String *s);
extern void string_append(String *s, char c);
extern void string_appendf(String *s, char *fmt, ...);

#define STRING(x)                                                       \
    (String){ .body = (x), .nalloc = sizeof(x), .len = sizeof(x) + 1 }

enum {
    TTYPE_IDENT,
    TTYPE_PUNCT,
    TTYPE_NUMBER,
    TTYPE_CHAR,
    TTYPE_STRING,
    // Only in CPP
    TTYPE_NEWLINE,
    TTYPE_SPACE,
    TTYPE_MACRO_PARAM,
};

typedef struct {
    int type;
    bool space;
    bool bol;
    char *file;
    int line;
    int column;
    Dict *hideset;
    union {
        char *sval;
        int punct;
        char c;
        union {
            int position;
        };
    };
} Token;

enum {
    AST_LITERAL = 256,
    AST_STRING,
    AST_LVAR,
    AST_GVAR,
    AST_FUNCALL,
    AST_FUNC,
    AST_DECL,
    AST_INIT_LIST,
    AST_ADDR,
    AST_DEREF,
    AST_IF,
    AST_TERNARY,
    AST_FOR,
    AST_WHILE,
    AST_DO,
    AST_SWITCH,
    AST_CASE,
    AST_DEFAULT,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_COMPOUND_STMT,
    AST_STRUCT_REF,
    AST_GOTO,
    AST_LABEL,
    AST_VA_START,
    AST_VA_ARG,
    OP_EQ,
    OP_NE,
    OP_LE,
    OP_GE,
    OP_INC,
    OP_DEC,
    OP_LSH,
    OP_RSH,
    OP_A_ADD,
    OP_A_SUB,
    OP_A_MUL,
    OP_A_DIV,
    OP_A_MOD,
    OP_A_AND,
    OP_A_OR,
    OP_A_XOR,
    OP_A_LSH,
    OP_A_RSH,
    OP_LOGAND,
    OP_LOGOR,
    OP_ARROW,
    OP_SIZEOF,
    OP_CAST,
};

enum {
    CTYPE_VOID,
    CTYPE_CHAR,
    CTYPE_SHORT,
    CTYPE_INT,
    CTYPE_LONG,
    CTYPE_LLONG,
    CTYPE_FLOAT,
    CTYPE_DOUBLE,
    CTYPE_LDOUBLE,
    CTYPE_ARRAY,
    CTYPE_PTR,
    CTYPE_STRUCT,
    CTYPE_FUNC,
    // used only in parser
    CTYPE_STUB,
};

typedef struct Ctype {
    int type;
    int size;
    // true if signed
    bool sig;
    // pointer or array
    struct Ctype *ptr;
    // array length
    int len;
    // struct
    Dict *fields;
    int offset;
    // function
    struct Ctype *rettype;
    List *params;
    bool hasva;
} Ctype;

typedef struct Node {
    int type;
    Ctype *ctype;
    union {
        // Char, int, or long
        long ival;
        // Float or double
        struct {
            double fval;
            char *flabel;
        };
        // String
        struct {
            char *sval;
            char *slabel;
        };
        // Local/global variable
        struct {
            char *varname;
            int loff;
            char *glabel;
        };
        // Binary operator
        struct {
            struct Node *left;
            struct Node *right;
        };
        // Unary operator
        struct {
            struct Node *operand;
        };
        // Function call or function declaration
        struct {
            char *fname;
            // Function call
            struct List *args;
            struct Ctype *ftype;
            // Function declaration
            struct List *params;
            struct List *localvars;
            struct Node *body;
            bool use_varargs;
        };
        // Declaration
        struct {
            struct Node *declvar;
            struct Node *declinit;
        };
        // array or struct initializer
        struct {
            struct List *initlist;
            Ctype *totype;
        };
        // If statement or ternary operator
        struct {
            struct Node *cond;
            struct Node *then;
            struct Node *els;
        };
        // For statement
        struct {
            struct Node *forinit;
            struct Node *forcond;
            struct Node *forstep;
            struct Node *forbody;
        };
        // Switch statement
        struct {
            struct Node *switchexpr;
            struct Node *switchbody;
        };
        // Switch-case label
        int caseval;
        // Goto and label
        struct {
            char *label;
            char *newlabel;
        };
        // Return statement
        struct Node *retval;
        // Compound statement
        struct List *stmts;
        // Struct reference
        struct {
            struct Node *struc;
            char *field;
            Ctype *fieldtype;
        };
        // Builtin functions for varargs
        struct Node *ap;
    };
} Node;

extern Ctype *ctype_char;
extern Ctype *ctype_short;
extern Ctype *ctype_int;
extern Ctype *ctype_long;
extern Ctype *ctype_float;
extern Ctype *ctype_double;

extern void cpp_init(void);
extern void unget_cpp_token(Token *tok);
extern Token *peek_cpp_token(void);
extern Token *read_cpp_token(void);
extern void set_input_buffer(List *tokens);
extern List *get_input_buffer(void);
extern void skip_cond_incl(void);
extern bool read_header_file_name(char **name, bool *std);
extern void push_input_file(char *filename, FILE *input);
extern void set_input_file(char *filename, FILE *input);
extern char *input_position(void);
extern char *get_current_file(void);
extern int get_current_line(void);

extern void unget_token(Token *tok);
extern Token *peek_token(void);
extern Token *read_token(void);
extern void expect_newline(void);

extern char *t2s(Token *tok);
extern bool is_punct(Token *tok, int c);
extern bool is_ident(Token *tok, char *s);
extern char *a2s(Node *node);
extern char *c2s(Ctype *ctype);
extern void print_asm_header(void);
extern char *make_label(void);
extern List *read_toplevels(void);
extern Node *read_expr(void);
extern int eval_intexpr(Node *node);
extern bool is_inttype(Ctype *ctype);
extern bool is_flotype(Ctype *ctype);
extern Ctype *result_type(int op, Ctype *a, Ctype *b);

extern void emit_data_section(void);
extern void emit_toplevel(Node *v);

extern List *strings;
extern List *flonums;

#endif /* EIGHTCC_H */
