#include <stdio.h>

extern int mymain(void);

int main(int argc, char **argv) {
  printf("%d\n", mymain());
  return 0;
}
