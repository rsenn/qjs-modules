#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include "vector.h"
#include "debug.h"
#include <assert.h>

/**
 * \defgroup ringbuffer ringbuffer: Ring Buffer implementation
 * @{
 */
typedef union ringbuffer {
  struct {
    uint8_t* data;
    size_t size, capacity;
    BOOL error;
    DynBufReallocFunc* realloc_func;
    void* opaque;
    volatile uint32_t tail, head;
  };
  DynBuf dbuf;
  Vector vec;

} RingBuffer;

#define RINGBUFFER_INIT() \
  { \
    { 0, 0, 0, 0, &ringbuffer_default_realloc, 0 } \
  }

#define ringbuffer_init(rb, size, ctx) \
  do { \
    vector_init(&(rb)->vec, ctx); \
    vector_allocate(&(rb)->vec, 1, ((size) - 1)); \
  } while(0)

#define ringbuffer_init_rt(rb, size, rt) \
  do { \
    vector_init_rt(&(rb)->vec, rt); \
    vector_allocate(&(rb)->vec, 1, ((size) - 1)); \
  } while(0)

#define RINGBUFFER(ctx) \
  (RingBuffer) { \
    { 0, 0, 0, 0, (DynBufReallocFunc*)&js_realloc, ctx, 0, 0 } \
  }

#define RINGBUFFER_RT(rt) \
  (RingBuffer) { \
    { 0, 0, 0, 0, (DynBufReallocFunc*)&js_realloc_rt, rt } \
  }

#define ringbuffer_FREE(rb) vector_free(&(rb)->vec)
#define ringbuffer_TAIL(rb) (void*)&(rb)->data[(rb)->tail]
#define ringbuffer_HEAD(rb) (void*)&(rb)->data[(rb)->head]

#define ringbuffer_EMPTY(rb) ((rb)->tail == (rb)->head)
#define ringbuffer_WRAPPED(rb) ((rb)->head < (rb)->tail)
#define ringbuffer_HEADROOM(rb) ((rb)->size - (rb)->head)
#define ringbuffer_TAILROOM(rb) ((rb)->size - (rb)->tail)
#define ringbuffer_AVAIL(rb) ((rb)->size - ringbuffer_LENGTH(rb) - 1)
#define ringbuffer_LENGTH(rb) \
  ((rb)->head >= (rb)->tail ? (rb)->head - (rb)->tail : (rb)->size - (rb)->tail + (rb)->head)
#define ringbuffer_CONTINUOUS(rb) (ringbuffer_WRAPPED(rb) ? (rb)->size - (rb)->tail : (rb)->head - (rb)->tail)
// #define ringbuffer_IS_CONTINUOUS(rb) ((rb)->head >= (rb)->tail)
// #define ringbuffer_SKIP(rb, n) ((rb)->tail += (n), (rb)->tail %= (rb)->size)
#define ringbuffer_WRAP(rb, idx) ((idx) % (rb)->size)
#define ringbuffer_NEXT(rb, ptr) (void*)(ringbuffer_WRAP(rb, ((uint8_t*)(ptr + 1)) - (rb)->data) + (rb)->data)

void ringbuffer_reset(RingBuffer*);
ssize_t ringbuffer_write(RingBuffer*, const void*, size_t);
ssize_t ringbuffer_read(RingBuffer*, void*, size_t);
uint8_t* ringbuffer_peek(RingBuffer*, size_t);
void ringbuffer_normalize(RingBuffer*);
BOOL ringbuffer_resize(RingBuffer*, size_t);
BOOL ringbuffer_reserve(RingBuffer*, size_t);
ssize_t ringbuffer_append(RingBuffer*, const void*, size_t);

static inline uint8_t*
ringbuffer_skip(RingBuffer* rb, size_t n) {
  assert(ringbuffer_LENGTH(rb) >= n);

  rb->tail = (rb->tail + n) % rb->size;

  return ringbuffer_TAIL(rb);
}

size_t ringbuffer_size(RingBuffer*);
size_t ringbuffer_length(RingBuffer*);
size_t ringbuffer_avail(RingBuffer*);

/**
 * @}
 */
#endif /* defined(RINGBUFFER_H) */
