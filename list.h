#ifndef EIGHTCC_LIST_H
#define EIGHTCC_LIST_H

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

#define EMPTY_LIST                                      \
    ((List){ .len = 0, .head = NULL, .tail = NULL })

List *make_list(void);
List *list_copy(List *list);
void list_push(List *list, void *elem);
void *list_pop(List *list);
void list_append(List *a, List *b);
void *list_shift(List *list);
void list_unshift(List *list, void *elem);
void *list_get(List *list, int index);
void *list_head(List *list);
void *list_tail(List *list);
List *list_reverse(List *list);
int list_len(List *list);
Iter *list_iter(List *list);
void *iter_next(Iter *iter);
bool iter_end(Iter *iter);

#endif /* EIGHTCC_LIST_H */
