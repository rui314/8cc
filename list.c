#include <stdlib.h>
#include "list.h"
#include "util.h"

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

static void list_unshift(List *list, void *elem) {
  ListNode *node = make_node(elem);
  node->next = list->head;
  list->head = node;
  if (!list->tail)
    list->tail = node;
  list->len++;
}

List *list_reverse(List *list) {
  List *r = make_list();
  for (Iter *i = list_iter(list); !iter_end(i);) {
    list_unshift(r, iter_next(i));
  }
  return r;
}

void *list_last(List *list) {
  if (!list->head) return NULL;
  ListNode *p = list->head;
  while (p->next) p = p->next;
  return p->elem;
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
