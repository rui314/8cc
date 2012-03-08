#include <stdlib.h>
#include "list.h"

List *make_list(void) {
  List *r = malloc(sizeof(List));
  r->len = 0;
  r->head = r->tail = NULL;
  return r;
}

void *make_node(void *elem) {
  ListNode *r = malloc(sizeof(ListNode));
  r->elem = elem;
  r->next = NULL;
  return r;
}

void list_append(List *list, void *elem) {
  ListNode *node = make_node(elem);
  if (!list->head)
    list->head = node;
  else
    list->tail->next = node;
  list->tail = node;
  list->len++;
}

int list_len(List *list) {
  return list->len;
}

Iter *list_iter(List *list) {
  Iter *r = malloc(sizeof(Iter));
  r->ptr = list->head;
  return r;
}

void *iter_next(Iter *iter) {
  if (!iter->ptr)
    return NULL;
  void *r = iter->ptr->elem;
  iter->ptr = iter->ptr->next;
  return r;
}

bool iter_end(Iter *iter) {
  return !iter->ptr;
}
