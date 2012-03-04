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

#define error(...)                              \
  errorf(__FILE__, __LINE__, __VA_ARGS__)

#define assert(expr)                                    \
  do {                                                  \
    if (!(expr)) error("Assertion failed: " #expr);     \
  } while (0)

extern void errorf(char *fmt, ...) __attribute__((noreturn));
extern void warn(char *fmt, ...);

extern String *make_string(void);
extern char *get_cstring(String *s);
extern void string_append(String *s, char c);
extern void string_appendf(String *s, char *fmt, ...);

extern char *token_to_string(Token *tok);
extern bool is_punct(Token *tok, char c);
extern void unget_token(Token *tok);
extern Token *peek_token(void);
extern Token *read_token(void);

#endif /* ECC_H */
