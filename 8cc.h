#ifndef ECC_H
#define ECC_H

#include <stdbool.h>
#include "util.h"
#include "list.h"

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
    int punct;
    char c;
  };
} Token;

typedef struct {
  char *body;
  int nalloc;
  int len;
} String;

enum {
  AST_LITERAL = 256,
  AST_STRING,
  AST_LVAR,
  AST_GVAR,
  AST_FUNCALL,
  AST_FUNC,
  AST_DECL,
  AST_ARRAY_INIT,
  AST_ADDR,
  AST_DEREF,
  AST_IF,
  AST_FOR,
  AST_RETURN,
  PUNCT_EQ,
  PUNCT_INC,
  PUNCT_DEC,
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
  int type;
  Ctype *ctype;
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
    // Function call or function declaration
    struct {
      char *fname;
      struct {
        struct List *args;
        struct {
          struct List *params;
          struct List *locals;
          struct List *body;
        };
      };
    };
    // Declaration
    struct {
      struct Ast *declvar;
      struct Ast *declinit;
    };
    // Array initializer
    struct List *arrayinit;
    // If statement
    struct {
      struct Ast *cond;
      struct List *then;
      struct List *els;
    };
    // For statement
    struct {
      struct Ast *forinit;
      struct Ast *forcond;
      struct Ast *forstep;
      struct List *forbody;
    };
    // Return statement
    struct Ast *retval;
  };
} Ast;

extern String *make_string(void);
extern char *get_cstring(String *s);
extern void string_append(String *s, char c);
extern void string_appendf(String *s, char *fmt, ...);

extern char *token_to_string(Token *tok);
extern bool is_punct(Token *tok, int c);
extern void unget_token(Token *tok);
extern Token *peek_token(void);
extern Token *read_token(void);
extern char *ast_to_string(Ast *ast);
extern char *ctype_to_string(Ctype *ctype);
extern void print_asm_header(void);
extern char *make_label(void);
extern List *read_func_list(void);

extern void emit_data_section(void);
extern void emit_func(Ast *func);

extern List *globals;
extern List *locals;
extern Ctype *ctype_int;
extern Ctype *ctype_char;

#endif /* ECC_H */
