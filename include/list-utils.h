#ifndef LIST_UTILS_H
#define LIST_UTILS_H

#include <list.h>
#include "defines.h"

/**
 * \defgroup list-utils list-utils: Linked list utilities
 * @{
 */

size_t list_size(struct list_head*);
struct list_head* list_front(const struct list_head*);
struct list_head* list_back(const struct list_head*);
struct list_head list_unlink(struct list_head*, struct list_head*);
void __list_splice(struct list_head*, struct list_head*);
void list_splice(struct list_head*, struct list_head*);
void __list_sort(struct list_head*, int (*cmp)(struct list_head*, struct list_head*, void*), void*);
void __list_reverse(struct list_head*);

static inline struct list_head*
list_init_if_zero(struct list_head* head) {
  if(head->next == NULL && head->prev == NULL)
    init_list_head(head);

  return head;
}

#define list_first(list, type, member) list_entry(list_front((list)), type, member)
#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)

#define list_last(list, type, member) list_entry(list_back((list)), type, member)
#define list_last_entry(ptr, type, member) list_entry((ptr)->prev, type, member)

/**
 * @}
 */

#endif /* defined(LIST_UTILS_H) */