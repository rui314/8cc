// Copyright 2012 Rui Ueyama <rui314@gmail.com>
// This program is free software licensed under the MIT license.

#ifndef EIGHTCC_H
#define EIGHTCC_H

#include <stdbool.h>
#include <stdio.h>
#include "buffer.h"
#include "dict.h"
#include "error.h"
#include "map.h"
#include "vector.h"

enum {
    TIDENT,
    TKEYWORD,
    TNUMBER,
    TCHAR,
    TSTRING,
    // Only in CPP
    TNEWLINE,
    TSPACE,
    TMACRO_PARAM,
};

typedef struct {
    int kind;
    int space; // true if the token has a leading space
    bool bol;  // true if the token is at the beginning of a line
    char *file;
    int line;
    int column;
    Map *hideset;
    union {
        int id;     // TKEYWORD
        char *sval; // TSTRING
        char c;     // TCHAR
        // TMACRO_PARAM
        struct {
            bool is_vararg;
            int position;
        };
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
    AST_DEFAULT,
    AST_RETURN,
    AST_COMPOUND_STMT,
    AST_STRUCT_REF,
    AST_GOTO,
    AST_COMPUTED_GOTO,
    AST_LABEL,
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
#define op(name, _) name,
#define keyword(name, x, y) name,
#include "keyword.h"
#undef keyword
#undef op
};

enum {
    KIND_VOID,
    KIND_BOOL,
    KIND_CHAR,
    KIND_SHORT,
    KIND_INT,
    KIND_LONG,
    KIND_LLONG,
    KIND_FLOAT,
    KIND_DOUBLE,
    KIND_LDOUBLE,
    KIND_ARRAY,
    KIND_ENUM,
    KIND_PTR,
    KIND_STRUCT,
    KIND_FUNC,
    // used only in parser
    KIND_STUB,
};

typedef struct Type {
    int kind;
    int size;
    int align;
    bool usig; // true if unsigned
    bool isstatic;
    // pointer or array
    struct Type *ptr;
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
    struct Type *rettype;
    Vector *params;
    bool hasva;
    bool oldstyle;
} Type;

typedef struct {
    char *file;
    int line;
} SourceLoc;

typedef struct Node {
    int kind;
    Type *ty;
    SourceLoc *sourceLoc;
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
            Vector *lvarinit;
            // global
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
            struct Vector *args;
            struct Type *ftype;
            // Functoin pointer or function designator
            struct Node *fptr;
            // Function declaration
            struct Vector *params;
            struct Vector *localvars;
            struct Node *body;
        };
        // Declaration
        struct {
            struct Node *declvar;
            struct Vector *declinit;
        };
        // Initializer
        struct {
            struct Node *initval;
            int initoff;
            Type *totype;
        };
        // If statement or ternary operator
        struct {
            struct Node *cond;
            struct Node *then;
            struct Node *els;
        };
        // Goto and label
        struct {
            char *label;
            char *newlabel;
        };
        // Return statement
        struct Node *retval;
        // Compound statement
        struct Vector *stmts;
        // Struct reference
        struct {
            struct Node *struc;
            char *field;
            Type *fieldtype;
        };
    };
} Node;

extern Type *type_void;
extern Type *type_bool;
extern Type *type_char;
extern Type *type_short;
extern Type *type_int;
extern Type *type_long;
extern Type *type_llong;
extern Type *type_uchar;
extern Type *type_ushort;
extern Type *type_uint;
extern Type *type_ulong;
extern Type *type_ullong;
extern Type *type_float;
extern Type *type_double;
extern Type *type_ldouble;

extern void cpp_init(void);
extern void lex_init(char *filename);
extern char *get_base_file(void);
extern char *read_error_directive(void);
extern void unget_token(Token *tok);
extern Token *lex(void);
extern void set_input_buffer(Vector *tokens);
extern Vector *get_input_buffer(void);
extern int include_level_depth(void);
extern void skip_cond_incl(void);
extern char *read_header_file_name(bool *std);
extern void push_input_file(char *displayname, char *realname, FILE *input);
extern void set_input_file(char *displayname, char *realname, FILE *input);
extern char *input_position(void);
extern char *get_current_file(void);
extern char *fullpath(char *path);
extern int get_current_line(void);
extern char *get_current_displayname(void);
extern void set_current_displayname(char *name);
extern void set_current_line(int line);
extern void cpp_eval(char *buf);
extern void add_include_path(char *path);
extern void parse_init(void);
extern Token *peek_token(void);
extern Token *read_token(void);
extern void expect_newline(void);
extern bool is_keyword(Token *tok, int c);
extern bool is_ident(Token *tok, char *s);
extern char *make_label(void);
extern Vector *read_toplevels(void);
extern Node *read_expr(void);
extern int eval_intexpr(Node *node);
extern bool is_inttype(Type *ty);
extern bool is_flotype(Type *ty);

// Debug
extern bool debug_cpp;
extern char *t2s(Token *tok);
extern char *a2s(Node *node);
extern char *c2s(Type *ty);

// Gen
extern void emit_toplevel(Node *v);
extern void set_output_file(FILE *fp);
extern void close_output_file(void);

#endif
