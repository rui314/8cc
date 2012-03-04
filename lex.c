#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "8cc.h"

#define BUFLEN 256

static Token *ungotten = NULL;

static Token *make_ident(String *s) {
  Token *r = malloc(sizeof(Token));
  r->type = TTYPE_IDENT;
  r->sval = get_cstring(s);
  return r;
}

static Token *make_strtok(String *s) {
  Token *r = malloc(sizeof(Token));
  r->type = TTYPE_STRING;
  r->sval = get_cstring(s);
  return r;
}

static Token *make_punct(char punct) {
  Token *r = malloc(sizeof(Token));
  r->type = TTYPE_PUNCT;
  r->punct = punct;
  return r;
}

static Token *make_int(int ival) {
  Token *r = malloc(sizeof(Token));
  r->type = TTYPE_INT;
  r->ival = ival;
  return r;
}

static Token *make_char(char c) {
  Token *r = malloc(sizeof(Token));
  r->type = TTYPE_CHAR;
  r->c = c;
  return r;
}

static void skip_space(void) {
  int c;
  while ((c = getc(stdin)) != EOF) {
    if (isspace(c))
      continue;
    ungetc(c, stdin);
    return;
  }
}

static Token *read_number(char c) {
  int n = c - '0';
  for (;;) {
    int c = getc(stdin);
    if (!isdigit(c)) {
      ungetc(c, stdin);
      return make_int(n);
    }
    n = n * 10 + (c - '0');
  }
}

static Token *read_char(void) {
  char c = getc(stdin);
  if (c == EOF) goto err;
  if (c == '\\') {
    c = getc(stdin);
    if (c == EOF) goto err;
  }
  char c2 = getc(stdin);
  if (c2 == EOF) goto err;
  if (c2 != '\'')
    error("Malformed char literal");
  return make_char(c);
err:
  error("Unterminated char");
}

static Token *read_string(void) {
  String *s = make_string();
  for (;;) {
    int c = getc(stdin);
    if (c == EOF)
      error("Unterminated string");
    if (c == '"')
      break;
    if (c == '\\') {
      c = getc(stdin);
      if (c == EOF) error("Unterminated \\");
    }
    string_append(s, c);
  }
  return make_strtok(s);
}

static Token *read_ident(char c) {
  String *s = make_string();
  string_append(s, c);
  for (;;) {
    int c2 = getc(stdin);
    if (isalnum(c2) || c2 == '_') {
      string_append(s, c2);
    } else {
      ungetc(c2, stdin);
      return make_ident(s);
    }
  }
}

static Token *read_token_int(void) {
  skip_space();
  int c = getc(stdin);
  switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return read_number(c);
    case '"':
      return read_string();
    case '\'':
      return read_char();
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z': case 'A': case 'B':
    case 'C': case 'D': case 'E': case 'F': case 'G': case 'H': case 'I':
    case 'J': case 'K': case 'L': case 'M': case 'N': case 'O': case 'P':
    case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W':
    case 'X': case 'Y': case 'Z': case '_':
      return read_ident(c);
    case '/': case '=': case '*': case '+': case '-': case '(': case ')':
    case ',': case ';':
      return make_punct(c);
    case EOF:
      return NULL;
    default:
      error("Unexpected character: '%c'", c);
  }
}

char *token_to_string(Token *tok) {
  switch (tok->type) {
    case TTYPE_IDENT:
      return tok->sval;
    case TTYPE_PUNCT:
    case TTYPE_CHAR: {
      String *s = make_string();
      string_append(s, tok->c);
      return get_cstring(s);
    }
    case TTYPE_INT: {
      String *s = make_string();
      string_appendf(s, "%d", tok->ival);
      return get_cstring(s);
    }
    case TTYPE_STRING: {
      String *s = make_string();
      string_appendf(s, "\"%s\"", tok->sval);
      return get_cstring(s);
    }
    default:
      error("internal error: unknown token type: %d", tok->type);
  }
}

int is_punct(Token *tok, char c) {
  if (!tok)
    error("Token is null");
  return tok->type == TTYPE_PUNCT && tok->punct == c;
}

void unget_token(Token *tok) {
  if (ungotten)
    error("Push back buffer is already full");
  ungotten = tok;
}

Token *read_token(void) {
  if (ungotten) {
    Token *tok = ungotten;
    ungotten = NULL;
    return tok;
  }
  return read_token_int();
}
