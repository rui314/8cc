#include <stdio.h>

#define WEAK __attribute__((weak))
extern int intfn(void) WEAK;
extern char *stringfn(void) WEAK;

int main(int argc, char **argv) {
  if (intfn) {
    printf("%d\n", intfn());
  } else if (stringfn) {
    printf("%s\n", stringfn());
  } else {
    printf("Should not happen");
  }
  return 0;
}
