#include "queue.h"
#include <stdlib.h>
#include <string.h>

static inline block_t*
block_alloc(size_t a) {
  block_t* b;
  if((b = malloc(sizeof(block_t) + a))) {
    memset(b, 0, sizeof(block_t));
  }
  return b;
}

static inline void
block_free(block_t* b) {
  free(b);
}

/**
 * \addtogroup queue
 * @{
 */
ssize_t
queue_write(queue_t* q, const void* x, size_t n) {
  block_t* b;

  if((b = block_alloc(n))) {

    list_add(&b->head, &q->head);
    b->size = n;
    memcpy(b->data, x, n);
    q->nbytes += n;
    q->nblocks++;
    return n;
  }

  return -1;
}

ssize_t
queue_read(queue_t* q, void* x, size_t n) {
  block_t *b, *next;
  ssize_t ret = 0;
  uint8_t* p = x;

  while(n > 0 && (b = q->blocks.prev)) {
    size_t bytes = b->size - b->pos;
    if(bytes >= n)
      bytes = n;

    memcpy(p, &b->data[b->pos], bytes);
    p += bytes;
    n -= bytes;

    b->pos += bytes;
    ret += bytes;

    q->nbytes -= bytes;

    if(b->pos < b->size)
      break;

    next = (block_t*)b->link.prev;

    block_free(b);
    q->nblocks--;

    b = next;
  }

  return ret;
}

ssize_t
queue_peek(queue_t* q, void* x, size_t n) {
  block_t *b, *next;
  ssize_t ret = 0;
  uint8_t* p = x;

  while(n > 0 && (b = q->blocks.prev)) {
    size_t bytes = b->size - b->pos;
    if(bytes >= n)
      bytes = n;

    next = (block_t*)b->link.prev;

    memcpy(p, &b->data[b->pos], bytes);
    p += bytes;
    n -= bytes;

    ret += bytes;

    if(b->pos < b->size)
      break;

    b = next;
  }

  return ret;
}

/**
 * @}
 */
