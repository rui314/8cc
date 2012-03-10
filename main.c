#include <stdio.h>
#include <string.h>
#include "8cc.h"

int main(int argc, char **argv) {
  int wantast = (argc > 1 && !strcmp(argv[1], "-a"));
  List *funcs = read_func_list();
  if (!wantast)
    emit_data_section();
  for (Iter *i = list_iter(funcs); !iter_end(i);) {
    Ast *func = iter_next(i);
    if (wantast)
      printf("%s", ast_to_string(func));
    else
      emit_func(func);
  }
  return 0;
}
