#ifndef ECC_H
#define ECC_H

#include <stdbool.h>

enum {
  TTYPE_IDENT,
  TTYPE_PUNCT,
  TTYPE_INT,
  TTYPE_CHAR,
  TTYPE_STRING,
};

typedef struct {
  int type;
  union {
    int ival;
    char *sval;
    char punct;
    char c;
  };
} Token;

typedef struct {
  char *body;
  int nalloc;
  int len;
} String;

enum {
  AST_LITERAL,
  AST_STRING,
  AST_LVAR,
  AST_LREF,
  AST_GVAR,
  AST_GREF,
  AST_FUNCALL,
  AST_DECL,
  AST_ARRAY_INIT,
  AST_ADDR,
  AST_DEREF,
  AST_IF,
};

enum {
  CTYPE_VOID,
  CTYPE_INT,
  CTYPE_CHAR,
  CTYPE_ARRAY,
  CTYPE_PTR,
};

typedef struct Ctype {
  int type;
  struct Ctype *ptr;
  int size;
} Ctype;

typedef struct Ast {
  char type;
  Ctype *ctype;
  struct Ast *next;
  union {
    // Integer
    int ival;
    // Char
    char c;
    // String
    struct {
      char *sval;
      char *slabel;
    };
    // Local variable
    struct {
      char *lname;
      int loff;
    };
    // Global variable
    struct {
      char *gname;
      char *glabel;
    };
    // Local reference
    struct {
      struct Ast *lref;
      int lrefoff;
    };
    // Global reference
    struct {
      struct Ast *gref;
      int goff;
    };
    // Binary operator
    struct {
      struct Ast *left;
      struct Ast *right;
    };
    // Unary operator
    struct {
      struct Ast *operand;
    };
    // Function call
    struct {
      char *fname;
      int nargs;
      struct Ast **args;
    };
    // Declaration
    struct {
      struct Ast *declvar;
      struct Ast *declinit;
    };
    // Array initializer
    struct {
      int size;
      struct Ast **array_init;
    };
    // If statement
    struct {
      struct Ast *cond;
      struct Ast **then;
      struct Ast **els;
    };
  };
} Ast;

#define error(...)                              \
  errorf(__FILE__, __LINE__, __VA_ARGS__)

#define assert(expr)                                    \
  do {                                                  \
    if (!(expr)) error("Assertion failed: " #expr);     \
  } while (0)

extern void errorf(char *file, int line, char *fmt, ...) __attribute__((noreturn));
extern void warn(char *fmt, ...);
extern char *quote_cstring(char *p);

extern String *make_string(void);
extern char *get_cstring(String *s);
extern void string_append(String *s, char c);
extern void string_appendf(String *s, char *fmt, ...);

extern char *token_to_string(Token *tok);
extern bool is_punct(Token *tok, char c);
extern void unget_token(Token *tok);
extern Token *peek_token(void);
extern Token *read_token(void);
extern char *ast_to_string(Ast *ast);
extern char *block_to_string(Ast **block);
extern char *ctype_to_string(Ctype *ctype);
extern void print_asm_header(void);
extern void emit_block(Ast **block);
extern char *make_next_label(void);

extern Ast **read_block(void);

extern Ast *globals;
extern Ast *locals;
extern Ctype *ctype_int;
extern Ctype *ctype_char;

#endif /* ECC_H */
