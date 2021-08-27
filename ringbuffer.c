#include "ringbuffer.h"
void
ringbuffer_reset(RingBuffer* r) {
  r->wr = r->rd = 0;
}

void
ringbuffer_queue(RingBuffer* r, uint8_t data) {
  /* overwrite the oldest byte if the r is full */
  if(ringbuffer_full(r)) {
    r->rd = ((r->rd + 1) % r->size);
  }

  r->data[r->wr] = data;
  r->wr = ((r->wr + 1) % r->size);
}

uint8_t
ringbuffer_dequeue(RingBuffer* r, uint8_t* data) {
  if(ringbuffer_empty(r))
    return 0;

  *data = r->data[r->rd];
  r->rd = ((r->rd + 1) % r->size);
  return 1;
}

ssize_t
ringbuffer_write(RingBuffer* r, const void* x, size_t len) {
  const uint8_t* p = x;
  size_t i;

  if(ringbuffer_avail(r) < len)
    ringbuffer_realloc(r, ringbuffer_length(r) + len);

  for(i = 0; i < len; i++) {
    assert(!ringbuffer_full(r));
    /*if(ringbuffer_full(r))
      break;*/
    ringbuffer_queue(r, p[i]);
  }

  return i;
}

ssize_t
ringbuffer_read(RingBuffer* r, void* x, size_t len) {
  uint8_t* p = x;
  size_t i;

  for(i = 0; i < len; i++) {
    if(ringbuffer_empty(r))
      break;
    ringbuffer_dequeue(r, &p[i]);
  }

  return i;
}

uint8_t
ringbuffer_peek(RingBuffer* r, uint8_t* data, size_t index) {
  size_t data_index;
  if(index >= ringbuffer_length(r))
    return 0;

  data_index = (r->rd + index) % r->size;
  *data = r->data[data_index];
  return 1;
}

void
ringbuffer_normalize(RingBuffer* r) {
  if(r->wr < r->rd) {
    size_t n = r->size - r->rd;
    void* x = alloca(r->wr);
    memcpy(x, r->data, r->wr);
    memmove(r->data, &r->data[r->rd], n);
    memcpy(&r->data[n], x, r->wr);
    r->rd = 0;
    r->wr += n;
    return;
  }

  memcpy(r->data, &r->data[r->rd], ringbuffer_length(r));
  r->wr -= r->rd;
  r->rd = 0;
}

void
ringbuffer_resize(RingBuffer* r, size_t newsize) {
  ringbuffer_normalize(r);
  if(newsize > r->size)
    vector_grow(&r->vec, 1, newsize);
  else if(newsize < r->size)
    vector_shrink(&r->vec, 1, newsize);
}
