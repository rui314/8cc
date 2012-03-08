#include <stdio.h>
#include <string.h>
#include "8cc.h"

int main(int argc, char **argv) {
  int wantast = (argc > 1 && !strcmp(argv[1], "-a"));
  List *block = read_block();
  if (wantast) {
    printf("%s", block_to_string(block));
  } else {
    print_asm_header();
    emit_block(block);
    printf("leave\n\t"
           "ret\n");
  }
  return 0;
}
