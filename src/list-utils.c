#include "list-utils.h"

size_t
list_size(struct list_head* list) {
  struct list_head* el;
  size_t i = 0;

  list_for_each(el, list) {
    ++i;
  }

  return i;
}

struct list_head*
list_front(const struct list_head* list) {
  return list->next != list ? list->next : 0;
}

struct list_head*
list_back(const struct list_head* list) {
  return list->prev != list ? list->prev : 0;
}

/**
 * Unlink a range of nodes from a list and isolate them.
 *
 * @param start  the first node of the range to unlink.
 * @param end    the node immediately following the range to unlink.
 * @return       a struct list_head representing the unlinked range boundaries.
 */
struct list_head
list_unlink(struct list_head* start, struct list_head* end) {
  struct list_head* prev = start->prev;
  struct list_head* last = end->prev;

  start->prev = NULL;
  last->next = NULL;

  prev->next = end;
  end->prev = prev;

  return (struct list_head){last, start};
}

void
__list_splice(struct list_head* list, struct list_head* head) {
  struct list_head* first = list->next;
  struct list_head* last = list->prev;
  struct list_head* at = head->next;

  first->prev = head;
  head->next = first;

  last->next = at;
  at->prev = last;
}

/**
 * join two lists by inserting the source list immediately after
 * the specified target head node.
 *
 * @param list  the new list to add.
 * @param head  the place to add it in the first list.
 */
void
list_splice(struct list_head* list, struct list_head* head) {
  if(!list_empty(list))
    __list_splice(list, head);
}

void
__list_sort(struct list_head* head, int (*cmp)(struct list_head* a, struct list_head* b, void*), void* opaque) {
  struct list_head *p, *q, *e, *list, *tail, *oldhead;
  int insize, nmerges, psize, qsize, i;

  list = head->next;
  list_del(head);
  insize = 1;

  for(;;) {
    p = oldhead = list;
    list = tail = NULL;
    nmerges = 0;

    while(p) {
      nmerges++;
      q = p;
      psize = 0;

      for(i = 0; i < insize; i++) {
        psize++;

        if(!(q = q->next == oldhead ? NULL : q->next))
          break;
      }

      qsize = insize;
      while(psize > 0 || (qsize > 0 && q)) {
        if(!psize) {
          e = q;
          q = q->next;
          qsize--;

          if(q == oldhead)
            q = NULL;
        } else if(!qsize || !q) {
          e = p;
          p = p->next;
          psize--;

          if(p == oldhead)
            p = NULL;
        } else if(cmp(p, q, opaque) <= 0) {
          e = p;
          p = p->next;
          psize--;
          if(p == oldhead)
            p = NULL;
        } else {
          e = q;
          q = q->next;
          qsize--;

          if(q == oldhead)
            q = NULL;
        }

        if(tail)
          tail->next = e;
        else
          list = e;

        e->prev = tail;
        tail = e;
      }

      p = q;
    }

    tail->next = list;
    list->prev = tail;

    if(nmerges <= 1)
      break;

    insize *= 2;
  }

  head->next = list;
  head->prev = list->prev;
  list->prev->next = head;
  list->prev = head;
}

void
__list_reverse(struct list_head* head) {
  if(!list_empty(head)) {
    struct list_head *p, *q, *list, *tail;

    list = head->next;
    tail = head->prev;
    list_del(head);
    init_list_head(head);

    for(p = list; (q = p->next); p = q) {
      list_add(p, head);

      if(p == tail)
        break;
    }
  }
}
