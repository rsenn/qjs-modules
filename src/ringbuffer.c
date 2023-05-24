#include "ringbuffer.h"

/**
 * \addtogroup ringbuffer
 * @{
 */
void
ringbuffer_reset(RingBuffer* r) {
  r->head = r->tail = 0;
}

void
ringbuffer_queue(RingBuffer* r, uint8_t data) {
  /* overwrite the oldest byte if the r is full */
  if(ringbuffer_full(r)) {
    r->tail = ((r->tail + 1) % r->size);
  }

  r->data[r->head] = data;
  r->head = ((r->head + 1) % r->size);
}

BOOL
ringbuffer_dequeue(RingBuffer* r, uint8_t* data) {
  if(ringbuffer_empty(r))
    return FALSE;

  *data = r->data[r->tail];
  r->tail = ((r->tail + 1) % r->size);
  return TRUE;
}

ssize_t
ringbuffer_write(RingBuffer* r, const void* x, size_t len) {
  const uint8_t* p = x;
  size_t i;

  if(ringbuffer_avail(r) < len)
    return -1;
  //    ringbuffer_realloc(r, ringbuffer_length(r) + len);

  for(i = 0; i < len; i++) {
    assert(!ringbuffer_full(r));
    ringbuffer_queue(r, p[i]);
  }

  return i;
}

ssize_t
ringbuffer_append(RingBuffer* r, const void* x, size_t len, JSContext* ctx) {

  if(!ringbuffer_reserve(r, ringbuffer_length(r) + len))
    return -1;

  return ringbuffer_write(r, x, len);
}

ssize_t
ringbuffer_read(RingBuffer* r, void* x, size_t len) {
  uint8_t* p = x;
  size_t i;

  if(ringbuffer_empty(r))
    return -1;

  for(i = 0; i < len; i++)
    ringbuffer_dequeue(r, &p[i]);

  return i;
}

uint8_t*
ringbuffer_peek(RingBuffer* r, size_t index) {
  if(index >= ringbuffer_length(r))
    return 0;

  return &r->data[(r->tail + index) % r->size];
}

void
ringbuffer_normalize(RingBuffer* r) {
  if(r->head < r->tail) {
    size_t n = r->size - r->tail;
    void* x = alloca(r->head);
    memcpy(x, r->data, r->head);
    memmove(r->data, &r->data[r->tail], n);
    memcpy(&r->data[n], x, r->head);
    r->tail = 0;
    r->head += n;
    return;
  }

  memcpy(r->data, &r->data[r->tail], ringbuffer_length(r));
  r->head -= r->tail;
  r->tail = 0;
}

BOOL
ringbuffer_resize(RingBuffer* r, size_t newsize) {
  ringbuffer_normalize(r);
  if(newsize > r->size)
    return vector_grow(&r->vec, 1, newsize);
  else if(newsize < r->size)
    return vector_shrink(&r->vec, 1, newsize);
  return TRUE;
}

BOOL
ringbuffer_allocate(RingBuffer* r, size_t size) {
  ssize_t n = ringbuffer_length(r);

  if((r->size - n) < size)
    return ringbuffer_resize(r, ringbuffer_length(r) + size);

  return TRUE;
}

uint8_t*
ringbuffer_reserve(RingBuffer* rb, size_t min_bytes) {
  ssize_t grow;
  if((grow = min_bytes - ringbuffer_avail(rb)) > 0)
    if(!ringbuffer_resize(rb, vector_size(&rb->vec, 1) + grow))
      return 0;

  if(ringbuffer_headroom(rb) < min_bytes)
    ringbuffer_normalize(rb);

  assert(ringbuffer_headroom(rb) >= min_bytes);

  return ringbuffer_end(rb);
}

/**
 * @}
 */
