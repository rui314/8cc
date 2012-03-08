#include <stdio.h>
#include <string.h>
#include "8cc.h"

void assert_string_equal(char *s, char *t) {
  if (strcmp(s, t))
    error("Expected %s but got %s", s, t);
}

void assert_int_equal(long a, long b) {
  if (a != b)
    error("Expected %ld but got %ld", a, b);
}

void test_string(void) {
  String *s = make_string();
  string_append(s, 'a');
  assert_string_equal("a", get_cstring(s));
  string_append(s, 'b');
  assert_string_equal("ab", get_cstring(s));

  string_appendf(s, ".");
  assert_string_equal("ab.", get_cstring(s));
  string_appendf(s, "%s", "0123456789");
  assert_string_equal("ab.0123456789", get_cstring(s));
}

void test_list(void) {
  List *list = make_list();
  list_append(list, (void *)1);
  list_append(list, (void *)2);
  Iter *iter = list_iter(list);
  assert_int_equal(1, (long)iter_next(iter));
  assert_int_equal(false, iter_end(iter));
  assert_int_equal(2, (long)iter_next(iter));
  assert_int_equal(true, iter_end(iter));
  assert_int_equal(0, (long)iter_next(iter));
  assert_int_equal(true, iter_end(iter));
}

int main(int argc, char **argv) {
  test_string();
  test_list();
  printf("Passed\n");
  return 0;
}
