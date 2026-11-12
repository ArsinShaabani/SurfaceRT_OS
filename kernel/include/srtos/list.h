/* SurfaceRT_OS - intrusive doubly linked list (Linux style) */
#ifndef SRTOS_LIST_H
#define SRTOS_LIST_H

#include <srtos/types.h>

struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static inline void list_init(struct list_head *l)
{
    l->next = l;
    l->prev = l;
}

static inline void __list_add(struct list_head *n, struct list_head *prev,
                              struct list_head *next)
{
    next->prev = n;
    n->next = next;
    n->prev = prev;
    prev->next = n;
}

static inline void list_add(struct list_head *n, struct list_head *head)
{
    __list_add(n, head, head->next);
}

static inline void list_add_tail(struct list_head *n, struct list_head *head)
{
    __list_add(n, head->prev, head);
}

static inline void list_del(struct list_head *e)
{
    e->prev->next = e->next;
    e->next->prev = e->prev;
    e->next = e->prev = e;
}

static inline int list_empty(const struct list_head *h)
{
    return h->next == h;
}

static inline int list_is_singular(const struct list_head *h)
{
    return !list_empty(h) && h->next == h->prev;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(head, type, member) \
    list_entry((head)->next, type, member)
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)
#define list_for_each_entry(e, head, member) \
    for (e = list_entry((head)->next, __typeof__(*e), member); \
         &e->member != (head); \
         e = list_entry(e->member.next, __typeof__(*e), member))
#define list_for_each_entry_safe(e, tmp, head, member) \
    for (e = list_entry((head)->next, __typeof__(*e), member), \
        tmp = list_entry(e->member.next, __typeof__(*e), member); \
         &e->member != (head); \
         e = tmp, tmp = list_entry(tmp->member.next, __typeof__(*tmp), member))

#endif
