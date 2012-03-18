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
    r->prev = NULL;
    return r;
}

List *list_copy(List *list) {
    List *r = make_list();
    for (Iter *i = list_iter(list); !iter_end(i);)
        list_push(r, iter_next(i));
    return r;
}

void list_push(List *list, void *elem) {
    ListNode *node = make_node(elem);
    if (!list->head) {
        list->head = node;
    } else {
        list->tail->next = node;
        node->prev = list->tail;
    }
    list->tail = node;
    list->len++;
}

void list_append(List *a, List *b) {
    for (Iter *i = list_iter(b); !iter_end(i);)
        list_push(a, iter_next(i));
}

void *list_pop(List *list) {
    if (!list->head) return NULL;
    void *r = list->tail->elem;
    list->tail = list->tail->prev;
    if (list->tail)
        list->tail->next = NULL;
    else
        list->head = NULL;
    list->len--;
    return r;
}

void *list_shift(List *list) {
    if (!list->head) return NULL;
    void *r = list->head->elem;
    list->head = list->head->next;
    if (list->head)
        list->head->prev = NULL;
    else
        list->tail = NULL;
    list->len--;
    return r;
}

void list_unshift(List *list, void *elem) {
    ListNode *node = make_node(elem);
    node->next = list->head;
    if (list->head)
        list->head->prev = node;
    list->head = node;
    if (!list->tail)
        list->tail = node;
    list->len++;
}

void *list_get(List *list, int index) {
    if (index < 0 || list->len <= index)
        return NULL;
    ListNode *p = list->head;
    for (int i = 0; i < index; i++)
        p = p->next;
    return p->elem;
}

void *list_head(List *list) {
    return list->head ? list->head->elem : NULL;
}

void *list_tail(List *list) {
    return list->tail ? list->tail->elem : NULL;
}

List *list_reverse(List *list) {
    List *r = make_list();
    for (Iter *i = list_iter(list); !iter_end(i);)
        list_unshift(r, iter_next(i));
    return r;
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
