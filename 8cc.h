// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_H
#define EIGHTCC_H

#include <stdbool.h>
#include <stdio.h>
#include "dict.h"
#include "list.h"
#include "error.h"

typedef struct {
    char *body;
    int nalloc;
    int len;
} String;

extern bool suppress_warning;

extern String *make_string(void);
extern char *format(char *fmt, ...);
extern char *vformat(char *fmt, va_list args);
extern char *get_cstring(String *s);
extern int string_len(String *s);
extern void string_append(String *s, char c);
extern void string_appendf(String *s, char *fmt, ...);

#define STRING(x)                                                       \
    (String){ .body = (x), .nalloc = sizeof(x), .len = sizeof(x) + 1 }

enum {
    TIDENT,
    TPUNCT,
    TNUMBER,
    TCHAR,
    TSTRING,
    // Only in CPP
    TNEWLINE,
    TSPACE,
    TMACRO_PARAM,
};

typedef struct {
    int type;
    int nspace;
    bool bol;
    bool is_vararg;
    char *file;
    int line;
    int column;
    Dict *hideset;
    union {
        char *sval;
        int punct;
        char c;
        int position;
    };
} Token;

enum {
    AST_LITERAL = 256,
    AST_STRING,
    AST_LVAR,
    AST_GVAR,
    AST_TYPEDEF,
    AST_FUNCALL,
    AST_FUNCPTR_CALL,
    AST_FUNCDESG,
    AST_FUNC,
    AST_DECL,
    AST_INIT,
    AST_CONV,
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
    AST_COMPUTED_GOTO,
    AST_LABEL,
    AST_VA_START,
    AST_VA_ARG,
    OP_UMINUS,
    OP_SIZEOF,
    OP_CAST,
    OP_SHR,
    OP_SHL,
    OP_A_SHR,
    OP_A_SHL,
    OP_PRE_INC,
    OP_PRE_DEC,
    OP_POST_INC,
    OP_POST_DEC,
    OP_LABEL_ADDR,
#define punct(name, _1) name,
#define keyword(name, _1, _2) name,
#include "keyword.h"
#undef keyword
#undef punct
};

enum {
    CTYPE_VOID,
    CTYPE_BOOL,
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
    bool isstatic;
    // pointer or array
    struct Ctype *ptr;
    // array length
    int len;
    // struct
    Dict *fields;
    int offset;
    bool is_struct; // true if struct, false if union
    // bitfield
    int bitoff;
    int bitsize;
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
            // local
            int loff;
            List *lvarinit;
            // global
            char *glabel;
        };
        // Typedef
        char *typedefname;
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
            // Functoin pointer or function designator
            struct Node *fptr;
            // Function declaration
            struct List *params;
            struct List *localvars;
            struct Node *body;
        };
        // Declaration
        struct {
            struct Node *declvar;
            struct List *declinit;
        };
        // Initializer
        struct {
            struct Node *initval;
            int initoff;
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
        struct {
            int casebeg;
            int caseend;
        };
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

typedef struct {
    void *first;
    void *second;
} Pair;

extern void *make_pair(void *first, void *second);

extern Ctype *ctype_char;
extern Ctype *ctype_short;
extern Ctype *ctype_int;
extern Ctype *ctype_long;
extern Ctype *ctype_float;
extern Ctype *ctype_double;

extern void cpp_init(void);
extern void lex_init(char *filename);
extern char *read_error_directive(void);
extern void unget_cpp_token(Token *tok);
extern Token *peek_cpp_token(void);
extern Token *read_cpp_token(void);
extern void set_input_buffer(List *tokens);
extern List *get_input_buffer(void);
extern void skip_cond_incl(void);
extern char *read_header_file_name(bool *std);
extern void push_input_file(char *displayname, char *realname, FILE *input);
extern void set_input_file(char *displayname, char *realname, FILE *input);
extern char *input_position(void);
extern char *get_current_file(void);
extern int get_current_line(void);
extern char *get_current_displayname(void);
extern void set_current_displayname(char *name);
extern void set_current_line(int line);
extern void cpp_eval(char *buf);
extern void add_include_path(char *path);

extern void parse_init(void);
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

extern void emit_toplevel(Node *v);
extern void set_output_file(FILE *fp);
extern void close_output_file(void);

extern bool debug_cpp;

#endif /* EIGHTCC_H */
