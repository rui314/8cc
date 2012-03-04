#include <stdio.h>
#include <string.h>
#include "8cc.h"

void assert_equal(char *s, char *t) {
  if (strcmp(s, t))
    error("Expected %s but got %s", s, t);
}

void test_string() {
  String *s = make_string();
  string_append(s, 'a');
  assert_equal("a", get_cstring(s));
  string_append(s, 'b');
  assert_equal("ab", get_cstring(s));

  string_appendf(s, ".");
  assert_equal("ab.", get_cstring(s));
  string_appendf(s, "%s", "0123456789");
  assert_equal("ab.0123456789", get_cstring(s));
}

int main(int argc, char **argv) {
  test_string();
  printf("Passed\n");
  return 0;
}
