// Copyright 2012 Rui Ueyama. Released under the MIT license.

#ifndef EIGHTCC_H
#define EIGHTCC_H

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <time.h>

enum {
    TIDENT,
    TKEYWORD,
    TNUMBER,
    TCHAR,
    TSTRING,
    TEOF,
    TINVALID,
    // Only in CPP
    MIN_CPP_TOKEN,
    TNEWLINE,
    TSPACE,
    TMACRO_PARAM,
};

enum {
    ENC_NONE,
    ENC_CHAR16,
    ENC_CHAR32,
    ENC_UTF8,
    ENC_WCHAR,
};

typedef struct Map {
    struct Map *parent;
    char **key;
    void **val;
    int size;
    int nelem;
    int nused;
} Map;

typedef struct {
    void **body;
    int len;
    int nalloc;
} Vector;

typedef struct {
    struct Map *map;
    Vector *key;
} Dict;

typedef struct Set {
    char *v;
    struct Set *next;
} Set;

typedef struct {
    char *body;
    int nalloc;
    int len;
} Buffer;

typedef struct {
    FILE *file;  // stream backed by FILE *
    char *p;     // stream backed by string
    char *name;
    int line;
    int column;
    int ntok;     // token counter
    int last;     // the last character read from file
    int buf[3];   // push-back buffer for unread operations
    int buflen;   // push-back buffer size
    time_t mtime; // last modified time. 0 if string-backed file
} File;

typedef struct {
    int kind;
    File *file;
    int line;
    int column;
    bool space;   // true if the token has a leading space
    bool bol;     // true if the token is at the beginning of a line
    int count;    // token number in a file, counting from 0.
    Set *hideset; // used by the preprocessor for macro expansion
    union {
        // TKEYWORD
        int id;
        // TSTRING or TCHAR
        struct {
            char *sval;
            int slen;
            int c;
            int enc;
        };
        // TMACRO_PARAM
        struct {
            bool is_vararg;
            int position;
        };
    };
} Token;

enum {
    AST_LITERAL = 256,
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
#include "keyword.inc"
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
            Vector *args;
            struct Type *ftype;
            // Function pointer or function designator
            struct Node *fptr;
            // Function declaration
            Vector *params;
            Vector *localvars;
            struct Node *body;
        };
        // Declaration
        struct {
            struct Node *declvar;
            Vector *declinit;
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
        Vector *stmts;
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

#define EMPTY_MAP ((Map){})
#define EMPTY_VECTOR ((Vector){})

// encoding.c
Buffer *to_utf16(char *p, int len);
Buffer *to_utf32(char *p, int len);
void write_utf8(Buffer *b, uint32_t rune);

// buffer.c
Buffer *make_buffer(void);
char *buf_body(Buffer *b);
int buf_len(Buffer *b);
void buf_write(Buffer *b, char c);
void buf_append(Buffer *b, char *s, int len);
void buf_printf(Buffer *b, char *fmt, ...);
char *vformat(char *fmt, va_list ap);
char *format(char *fmt, ...);
char *quote_cstring(char *p);
char *quote_cstring_len(char *p, int len);
char *quote_char(char c);

// cpp.c
void read_from_string(char *buf);
bool is_ident(Token *tok, char *s);
void expect_newline(void);
void add_include_path(char *path);
void init_now(void);
void cpp_init(void);
Token *peek_token(void);
Token *read_token(void);

// debug.c
char *ty2s(Type *ty);
char *node2s(Node *node);
char *tok2s(Token *tok);

// dict.c
Dict *make_dict(void);
void *dict_get(Dict *dict, char *key);
void dict_put(Dict *dict, char *key, void *val);
Vector *dict_keys(Dict *dict);

// error.c
extern bool enable_warning;
extern bool dumpstack;
extern bool dumpsource;
extern bool warning_is_error;

#define STR2(x) #x
#define STR(x) STR2(x)
#define error(...)       errorf(__FILE__ ":" STR(__LINE__), NULL, __VA_ARGS__)
#define errort(tok, ...) errorf(__FILE__ ":" STR(__LINE__), token_pos(tok), __VA_ARGS__)
#define warn(...)        warnf(__FILE__ ":" STR(__LINE__), NULL, __VA_ARGS__)
#define warnt(tok, ...)  warnf(__FILE__ ":" STR(__LINE__), token_pos(tok), __VA_ARGS__)

noreturn void errorf(char *line, char *pos, char *fmt, ...);
void warnf(char *line, char *pos, char *fmt, ...);
char *token_pos(Token *tok);

// file.c
File *make_file(FILE *file, char *name);
File *make_file_string(char *s);
int readc(void);
void unreadc(int c);
File *current_file(void);
void stream_push(File *file);
int stream_depth(void);
char *input_position(void);
void stream_stash(File *f);
void stream_unstash(void);

// gen.c
void set_output_file(FILE *fp);
void close_output_file(void);
void emit_toplevel(Node *v);

// lex.c
void lex_init(char *filename);
char *get_base_file(void);
void skip_cond_incl(void);
char *read_header_file_name(bool *std);
bool is_keyword(Token *tok, int c);
void token_buffer_stash(Vector *buf);
void token_buffer_unstash();
void unget_token(Token *tok);
Token *lex_string(char *s);
Token *lex(void);

// map.c
Map *make_map(void);
Map *make_map_parent(Map *parent);
void *map_get(Map *m, char *key);
void map_put(Map *m, char *key, void *val);
void map_remove(Map *m, char *key);
size_t map_len(Map *m);

// parse.c
char *make_tempname(void);
char *make_label(void);
bool is_inttype(Type *ty);
bool is_flotype(Type *ty);
void *make_pair(void *first, void *second);
int eval_intexpr(Node *node, Node **addr);
Node *read_expr(void);
Vector *read_toplevels(void);
void parse_init(void);
char *fullpath(char *path);

// set.c
Set *set_add(Set *s, char *v);
bool set_has(Set *s, char *v);
Set *set_union(Set *a, Set *b);
Set *set_intersection(Set *a, Set *b);

// vector.c
Vector *make_vector(void);
Vector *make_vector1(void *e);
Vector *vec_copy(Vector *src);
void vec_push(Vector *vec, void *elem);
void vec_append(Vector *a, Vector *b);
void *vec_pop(Vector *vec);
void *vec_get(Vector *vec, int index);
void vec_set(Vector *vec, int index, void *val);
void *vec_head(Vector *vec);
void *vec_tail(Vector *vec);
Vector *vec_reverse(Vector *vec);
void *vec_body(Vector *vec);
int vec_len(Vector *vec);

#endif
