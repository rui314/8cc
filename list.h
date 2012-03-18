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

extern List *make_list(void);
extern List *make_list1(void *e);
extern List *list_copy(List *list);
extern void list_push(List *list, void *elem);
extern void *list_pop(List *list);
extern void list_append(List *a, List *b);
extern void *list_shift(List *list);
extern void list_unshift(List *list, void *elem);
extern void *list_get(List *list, int index);
extern void *list_head(List *list);
extern void *list_tail(List *list);
extern List *list_reverse(List *list);
extern int list_len(List *list);
extern Iter *list_iter(List *list);
extern void *iter_next(Iter *iter);
extern bool iter_end(Iter *iter);

#endif /* EIGHTCC_LIST_H */
