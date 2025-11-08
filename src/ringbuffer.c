#include "ringbuffer.h"
#include <alloca.h>

/**
 * \addtogroup ringbuffer
 * @{
 */
void
ringbuffer_reset(RingBuffer* r) {
  r->head = r->tail = 0;
}

size_t
ringbuffer_size(RingBuffer* rb) {
  return rb->size;
}

size_t
ringbuffer_length(RingBuffer* rb) {
  return ringbuffer_LENGTH(rb);
}

size_t
ringbuffer_avail(RingBuffer* rb) {
  return ringbuffer_AVAIL(rb);
}

static BOOL
ringbuffer_dequeue(RingBuffer* r, uint8_t* data) {
  if(ringbuffer_EMPTY(r))
    return FALSE;

  *data = r->data[r->tail];
  r->tail = ((r->tail + 1) % r->size);

  return TRUE;
}

ssize_t
ringbuffer_write(RingBuffer* r, const void* x, size_t len) {
  const uint8_t* p = x;
  size_t n = ringbuffer_HEADROOM(r);

  if(ringbuffer_AVAIL(r) < len)
    return -1;

  memcpy(ringbuffer_HEAD(r), p, MIN_NUM(n, len));

  if(n >= len) {
    r->head += len;
  } else {
    r->head = len - n;
    memcpy(r->data, p + n, r->head);
  }

  return len;
}

ssize_t
ringbuffer_append(RingBuffer* r, const void* x, size_t len) {
  if(!ringbuffer_reserve(r, len))
    return -1;

  return ringbuffer_write(r, x, len);
}

ssize_t
ringbuffer_read(RingBuffer* r, void* x, size_t len) {
  uint8_t* p = x;
  size_t l, n = ringbuffer_TAILROOM(r);

  if(ringbuffer_EMPTY(r))
    return -1;

  if(len > (l = ringbuffer_LENGTH(r)))
    len = l;

  memcpy(p, ringbuffer_TAIL(r), MIN_NUM(n, len));

  if(n >= len) {
    r->tail += len;
  } else {
    r->tail = len - n;
    memcpy(p + n, r->data, r->tail);
  }

  return len;
}

uint8_t*
ringbuffer_peek(RingBuffer* r, size_t index) {
  if(index >= ringbuffer_LENGTH(r))
    return 0;

  return &r->data[(r->tail + index) % r->size];
}

void
ringbuffer_normalize(RingBuffer* r) {
  size_t l = ringbuffer_LENGTH(r);

  if(ringbuffer_WRAPPED(r)) {
    size_t n = ringbuffer_TAILROOM(r);

    /* Layout before:
     *   BBBBBB HEAD 00000000 TAIL AAAAA
     *
     * Layout after:
     *   TAIL AAAAA BBBBBB HEAD 00000000
     *
     * if length of AAAAA + HEAD <= TAIL then we can copy without temporary
     */
    if(n + r->head <= r->tail) {
      memmove(&r->data[n], r->data, r->head);
      memcpy(r->data, ringbuffer_TAIL(r), n);
    } else if(n > r->head) {
      void* b = alloca(r->head);

      memcpy(b, r->data, r->head);
      memmove(r->data, ringbuffer_TAIL(r), n);
      memcpy(&r->data[n], b, r->head);
    } else {
      void* a = alloca(n);

      memcpy(a, ringbuffer_TAIL(r), n);
      memmove(&r->data[n], r->data, r->head);
      memcpy(r->data, a, n);
    }
  } else {
    memcpy(r->data, ringbuffer_TAIL(r), l);
  }

  r->head = l;
  r->tail = 0;
}

BOOL
ringbuffer_resize(RingBuffer* r, size_t newsize) {
  if(ringbuffer_WRAPPED(r))
    ringbuffer_normalize(r);

  if(newsize > r->size)
    return vector_resize(&r->vec, 1, newsize);
  else if(newsize < r->size)
    return vector_shrink(&r->vec, 1, newsize);

  return TRUE;
}

BOOL
ringbuffer_reserve(RingBuffer* r, size_t size) {
  if(ringbuffer_AVAIL(r) < size)
    return ringbuffer_resize(r, ringbuffer_LENGTH(r) + size + 1);

  return TRUE;
}

/**
 * @}
 */
