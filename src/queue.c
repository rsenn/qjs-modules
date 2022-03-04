#include "queue.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"

/**
 * \addtogroup queue
 * @{
 */
Chunk*
chunk_alloc(size_t size) {
  Chunk* ch;
  if((ch = malloc(sizeof(Chunk) + size))) {
    memset(ch, 0, sizeof(Chunk));
    ch->ref_count = 1;
  }
  return ch;
}

void
chunk_free(Chunk* ch) {
  if(--ch->ref_count == 0)
    free(ch);
}

static void
chunk_arraybuffer_free(JSRuntime* rt, void* opaque, void* ptr) {
  Chunk* ch = opaque;
  chunk_free(ch);
}

JSValue
chunk_arraybuffer(Chunk* ch, JSContext* ctx) {
  uint8_t* ptr = ch->data + ch->pos;
  size_t len = ch->size - ch->pos;

  chunk_dup(ch);

  return JS_NewArrayBuffer(ctx, ptr, len, chunk_arraybuffer_free, ch, FALSE);
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
  Chunk* b;
  ssize_t ret = 0;
  uint8_t* p = x;

  while(n > 0 && (b = queue_tail(q))) {
    size_t bytes = MIN_NUM((b->size - b->pos), n);
    memcpy(p, &b->data[b->pos], bytes);
    p += bytes;
    n -= bytes;
    b->pos += bytes;
    ret += bytes;
    q->nbytes -= bytes;

    if(b->pos < b->size)
      break;

    list_del(&b->link);
    chunk_free(b);
    q->nblocks--;
  }

  return ret;
}

ssize_t
queue_peek(Queue* q, void* x, size_t n) {
  Chunk* b;
  ssize_t ret = 0;
  uint8_t* p = x;

  while(n > 0 && (b = queue_tail(q))) {
    size_t bytes = MIN_NUM((b->size - b->pos), n);

    memcpy(p, &b->data[b->pos], bytes);
    p += bytes;
    n -= bytes;
    ret += bytes;

    if(b->pos < b->size)
      break;
  }

  return ret;
}

ssize_t
queue_skip(Queue* q, size_t n) {
  Chunk* b;
  ssize_t ret = 0;

  while(n > 0 && (b = queue_tail(q))) {
    size_t bytes = MIN_NUM((b->size - b->pos), n);

    n -= bytes;
    b->pos += bytes;
    ret += bytes;
    q->nbytes -= bytes;

    if(b->pos < b->size)
      break;

    list_del(&b->link);
    chunk_free(b);
    q->nblocks--;
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
