#include <stdbool.h>

typedef struct ListNode {
  void *elem;
  struct ListNode *next;
  struct ListNode *prev;
} ListNode;

typedef struct List {
  int len;
  ListNode *head;
  ListNode *tail;
} List;

typedef struct Iter {
  ListNode *ptr;
} Iter;

List *make_list(void);
void list_push(List *list, void *elem);
void *list_pop(List *list);
List *list_reverse(List *list);
int list_len(List *list);
void *list_last(List *list);
Iter *list_iter(List *list);
void *iter_next(Iter *iter);
bool iter_end(Iter *iter);

#define EMPTY_LIST                                      \
  ((List){ .len = 0, .head = NULL, .tail = NULL })
