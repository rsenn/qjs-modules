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

ssize_t
chunk_size(Chunk* ch) {
  return ch->size - ch->pos;
}

ssize_t
chunk_tailpos(Chunk* ch, Queue* q) {
  ssize_t pos = 0;

  for(struct list_head* el = ch->link.next; el != &q->list; el = el->next) {
    Chunk* ch = list_entry(el, Chunk, link);

    pos += chunk_size(ch);
  }

  return pos;
}

ssize_t
chunk_headpos(Chunk* ch, Queue* q) {
  ssize_t pos = 0;

  for(struct list_head* el = &ch->link; el != &q->list; el = el->prev) {
    Chunk* ch = list_entry(el, Chunk, link);

    pos -= chunk_size(ch);
  }

  return pos;
}

void
queue_init(Queue* q) {
  init_list_head(&q->list);

  q->nbytes = 0;
  q->nchunks = 0;
}

ssize_t
queue_write(Queue* q, const void* x, size_t n) {
  Chunk* b;

  if((b = chunk_alloc(n))) {
    list_add(&b->link, &q->list);

    b->size = n;

    memcpy(b->data, x, n);

    q->nbytes += n;
    q->nchunks++;

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
    q->nchunks--;
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

    q->nchunks--;
  }
  
  return ret;
}

Chunk*
queue_next(Queue* q) {
  Chunk* chunk;

  if(!(chunk = queue_tail(q)))
    return 0;

  list_del(&chunk->link);

  --q->nchunks;
  q->nbytes -= chunk->size;

  return chunk;
}

void
queue_clear(Queue* q) {
  struct list_head *el, *el1;

  list_for_each_prev_safe(el, el1, &q->list) {
    Chunk* chunk = list_entry(el, Chunk, link);

    --q->nchunks;
    q->nbytes -= chunk->size;

    list_del(&chunk->link);
    chunk_free(chunk);
  }

  assert(list_empty(&q->list));
  assert(q->nchunks == 0);
  assert(q->nbytes == 0);
}

Chunk*
queue_chunk(Queue* q, ssize_t pos) {
  struct list_head* el;
  ssize_t i = 0;

  if(pos >= 0 && pos < q->nchunks) {
    list_for_each(el, &q->list) {

      if(i == pos)
        return list_entry(el, Chunk, link);

      ++i;
    }

  } else if(pos < 0 && pos >= -q->nchunks) {
    list_for_each_prev(el, &q->list) {
      --i;

      if(i == pos)
        return list_entry(el, Chunk, link);
    }
  }

  return NULL;
}

Chunk*
queue_at(Queue* q, ssize_t offset, size_t* skip) {
  struct list_head* el;
  ssize_t i = 0;

  if(offset >= 0 && offset < q->nbytes) {
    list_for_each(el, &q->list) {
      Chunk* chunk = list_entry(el, Chunk, link);
      ssize_t end = i + chunk_size(chunk);

      if(offset >= i && offset < end) {

        if(skip)
          *skip = offset - i;

        return chunk;
      }

      i = end;
    }

  } else if(offset < 0 && offset >= -q->nbytes) {
    list_for_each_prev(el, &q->list) {
      Chunk* chunk = list_entry(el, Chunk, link);
      ssize_t start = i - chunk_size(chunk);

      if(offset >= start && offset < i) {
        if(skip)
          *skip = offset - start;

        return chunk;
      }

      i = start;
    }
  }

  return NULL;
}

/**
 * @}
 */
