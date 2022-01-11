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

typedef struct queue {
  size_t nbytes;
  size_t nblocks;
  struct list_head blocks;
} queue_t;

typedef struct block {
  struct list_head link;
  uint32_t size, pos, allocated;
  uint8_t data[0];
} block_t;

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
