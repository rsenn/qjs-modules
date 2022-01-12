#ifndef QUEUE_H
#define QUEUE_H

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <list.h>

/**
 * \defgroup queue I/O queueing
 * @{
 */

struct link {
  struct block *prev, *next;
};

typedef struct queue {
  size_t nbytes;
  size_t nblocks;
  union {
    struct link blocks;
    struct list_head head;
  };
} queue_t;

struct block {
  union {
    struct link link;
    struct list_head head;
  };
  uint32_t size, pos, allocated;
  uint8_t data[0];
};

typedef struct block block_t;

ssize_t queue_write(queue_t*, const void*, size_t n);
ssize_t queue_read(queue_t*, void*, size_t n);
ssize_t queue_peek(queue_t*, void*, size_t n);

static inline size_t
queue_size(queue_t* q) {
  return q->nbytes;
}

/**
 * @}
 */
#endif /* defined(QUEUE_H) */
