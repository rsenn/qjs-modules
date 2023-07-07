#ifndef QUEUE_H
#define QUEUE_H

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <list.h>

/**
 * \defgroup queue queue: I/O queueing
 * @{
 */
typedef struct queue {
  size_t nbytes, nblocks;
  union {
    struct {
      struct block *tail, *head;
    };
    struct list_head list;
  };
} Queue;

typedef struct block {
  union {
    struct {
      struct block *prev, *next;
    };
    struct list_head link;
  };
  int ref_count;
  void* opaque;
  size_t size, pos;
  uint8_t data[0];
} Chunk;

Chunk* chunk_alloc(size_t);
void chunk_free(Chunk*);

static inline Chunk*
chunk_dup(Chunk* ch) {
  ++ch->ref_count;
  return ch;
}

void queue_init(Queue*);
ssize_t queue_write(Queue*, const void* x, size_t n);
ssize_t queue_read(Queue*, void* x, size_t n);
ssize_t queue_peek(Queue*, void* x, size_t n);
ssize_t queue_skip(Queue*, size_t n);
Chunk* queue_next(Queue*);
void queue_clear(Queue*);

static inline size_t
queue_size(Queue* q) {
  return q->nbytes;
}

static inline int
queue_empty(Queue* q) {
  return list_empty(&q->list);
}

static inline Chunk*
queue_head(Queue* q) {
  return (Chunk*)(&q->list != q->list.next ? q->list.next : 0);
}

static inline Chunk*
queue_tail(Queue* q) {
  return (Chunk*)(&q->list != q->list.prev ? q->list.prev : 0);
}

/**
 * @}
 */
#endif /* defined(QUEUE_H) */
