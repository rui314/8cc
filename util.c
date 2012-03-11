#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "8cc.h"

void errorf(char *file, int line, char *fmt, ...) {
  fprintf(stderr, "%s:%d: ", file, line);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
  exit(1);
}

void warn(char *fmt, ...) {
  fprintf(stderr, "warning: ");
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

char *quote_cstring(char *p) {
  String *s = make_string();
  while (*p) {
    if (*p == '\"' || *p == '\\')
      string_appendf(s, "\\%c", *p);
    else if (*p == '\n')
      string_appendf(s, "\\n");
    else
      string_append(s, *p);
    p++;
  }
  return get_cstring(s);
}
