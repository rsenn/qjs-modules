#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include "vector.h"

typedef union RingBuffer {
  struct {
    uint8_t* data;
    size_t size, capacity;
    BOOL error;
    DynBufReallocFunc* realloc_func;
    void* opaque;
    volatile uint32_t rd, wr;
  };
  DynBuf dbuf;
  Vector vec;

} RingBuffer;

#define RINGBUFFER_INIT()                                                                                              \
  {                                                                                                                    \
    { 0, 0, 0, 0, &ringbuffer_default_realloc, 0 }                                                                     \
  }

#define ringbuffer_init(rb, ctx) vector_init(&(rb)->vec, ctx)
#define ringbuffer_init_rt(rb, rt) vector_init_rt(&(rb)->vec, rt)
#define RINGBUFFER(ctx)                                                                                                \
  (RingBuffer) {                                                                                                       \
    { 0, 0, 0, 0, (DynBufReallocFunc*)&js_realloc, ctx, 0, 0 }                                                         \
  }
#define RINGBUFFER_RT(rt)                                                                                              \
  (RingBuffer) {                                                                                                       \
    { 0, 0, 0, 0, (DynBufReallocFunc*)&js_realloc_rt, rt }                                                             \
  }
#define ringbuffer_begin(rb) vector_begin(&(rb)->vec)
#define ringbuffer_end(rb) vector_end(&(rb)->vec)
#define ringbuffer_allocate(rb, elsz, pos) vector_allocate(&(rb)->vec, elsz, pos)
#define ringbuffer_shrink(rb, elsz, len) vector_shrink(&(rb)->vec, elsz, len)

#define ringbuffer_empty(rb) ((rb)->rd == (rb)->wr)
#define ringbuffer_full(rb) ((rb)->size == (rb)->wr - (rb)->rd)

void ringbuffer_reset(RingBuffer*);
void ringbuffer_queue(RingBuffer*, uint8_t data);
uint8_t ringbuffer_dequeue(RingBuffer*, uint8_t* data);
ssize_t ringbuffer_write(RingBuffer*, const void* x, size_t len);
ssize_t ringbuffer_read(RingBuffer*, void* x, size_t len);
uint8_t ringbuffer_peek(RingBuffer*, uint8_t* data, size_t index);
void ringbuffer_normalize(RingBuffer*);
void ringbuffer_resize(RingBuffer*, size_t newsize);

static inline size_t
ringbuffer_length(RingBuffer* rb) {
  return ((rb->wr - rb->rd) % rb->size);
}

static inline uint32_t
ringbuffer_avail(RingBuffer* rb) {
  if(rb->wr > rb->rd)
    return rb->wr - rb->rd;

  return rb->size - rb->wr + rb->rd;
}

#endif /* defined(RINGBUFFER_H) */
