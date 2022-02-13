#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include "debug.h"

/**
 * \addtogroup queue
 * @{
 */
Chunk*
chunk_alloc(size_t a) {
  Chunk* b;
  if((b = malloc(sizeof(Chunk) + a))) {
    memset(b, 0, sizeof(Chunk));
  }
  return b;
}

void
chunk_free(Chunk* b) {
  free(b);
}

void
queue_init(Queue* q) {
  init_list_head(&q->list);
  q->nbytes = 0;
  q->nblocks = 0;
}

ssize_t
queue_write(Queue* q, const void* x, size_t n) {
  Chunk* b;

  if((b = chunk_alloc(n))) {

    list_add(&b->link, &q->list);
    b->size = n;
    memcpy(b->data, x, n);
    q->nbytes += n;
    q->nblocks++;
    return n;
  }

  return -1;
}

ssize_t
queue_read(Queue* q, void* x, size_t n) {
  Chunk *b, *next;
  ssize_t ret = 0;
  uint8_t* p = x;

  while(n > 0 && (b = q->list.prev)) {
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

    next = (Chunk*)b->link.prev;

    chunk_free(b);
    q->nblocks--;

    b = next;
  }

  return ret;
}

ssize_t
queue_peek(Queue* q, void* x, size_t n) {
  Chunk *b, *next;
  ssize_t ret = 0;
  uint8_t* p = x;

  while(n > 0 && (b = q->list.prev)) {
    size_t bytes = b->size - b->pos;
    if(bytes >= n)
      bytes = n;

    next = (Chunk*)b->link.prev;

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

Chunk*
queue_next(Queue* q) {
  Chunk* chunk;

  if(!(chunk = queue_tail(q)))
    return 0;

  list_del(&chunk->link);

  --q->nblocks;
  q->nbytes -= chunk->size;

  return chunk;
}

void
queue_clear(Queue* q) {
  struct list_head *el, *el1;

  list_for_each_prev_safe(el, el1, &q->list) {
    Chunk* chunk = list_entry(el, Chunk, link);

    --q->nblocks;
    q->nbytes -= chunk->size;

    chunk_free(chunk);
  }

  assert(list_empty(&q->list));
  assert(q->nblocks == 0);
  assert(q->nbytes == 0);
}

/**
 * @}
 */
