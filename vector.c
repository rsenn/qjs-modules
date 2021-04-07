#define _GNU_SOURCE

#include "vector.h"
#include "utils.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_UINT128

#if(defined(__GNUC__) && (__GNUC__ >= 5)) || defined(HAVE__BUILTIN_MUL_OVERFLOW)
int
umult64(uint64_t a, uint64_t b, uint64_t* c) {
  return !__builtin_mul_overflow(a, b, c);
}
#elif defined(HAVE_UINT128)
int
umult64(uint64_t a, uint64_t b, uint64_t* c) {
  __uint128_t x = ((__uint128_t)a) * b;
  if((*c = (uint64_t)x) != x)
    return 0;
  return 1;
}
#else
int
umult64(uint64_t a, uint64_t b, uint64_t* c) {
  uint32_t ahi = a >> 32;
  uint32_t alo = (a & 0xffffffff);
  uint32_t bhi = b >> 32;
  uint32_t blo = (b & 0xffffffff);

  if(ahi && bhi)
    return 0;

  a = (uint64_t)(ahi)*blo + (uint64_t)(alo)*bhi;
  if(a > 0xffffffff)
    return 0;
  {
    uint64_t x = (uint64_t)(alo)*blo;
    if(x + (a << 32) < x)
      return 0;
    *c = x + (a << 32);
  }
  return 1;
}
#endif

#define roundto(n, mod) (((n) = (((n) + (mod)-1))), n = (n) - ((uint64_t)(n) % (uint64_t)(mod)))

void*
vector_allocate(Vector* vec, size_t elsz, int32_t pos) {
  uint64_t need;
  size_t capacity;
  if(pos < 0)
    return 0;
  if(!umult64(elsz, pos + 1, &need))
    return 0;

  if(need > vec->size) {
    capacity = vec->capacity;
    if(need > capacity) {
      if(elsz < 8)
        roundto(need, 1000);
      else
        roundto(need, 8000);
      assert(need >= 1000);
      if(dbuf_realloc(&vec->dbuf, need))
        return 0;
      if(vec->capacity > capacity)
        memset(vec->data + capacity, 0, vec->capacity - capacity);
      // vec->capacity += need;
    }
    vec->size = ((uint32_t)pos + 1) * elsz;
  }
  return vec->data + (uint32_t)pos * elsz;
}

void
vector_free(Vector* vec) {
  if(vec->data)
    dbuf_free(&vec->dbuf);
  vec->data = 0;
  vec->capacity = vec->size = 0;
}

void*
vector_at(const Vector* vec, size_t elsz, int32_t pos) {
  uint64_t offs;
  if(pos < 0)
    return 0;
  if(!umult64(elsz, pos, &offs))
    return 0;
  if(offs >= vec->size)
    return 0;
  return vec->data + offs;
}

int32_t
vector_indexof(const Vector* vec, size_t elsz, void* ptr) {
  if(ptr < vector_begin(vec) || ptr > vector_back(vec, elsz))
    return -1;

  return ((size_t)vector_begin(vec) - (size_t)ptr) / elsz;
}

void
vector_shrink(Vector* vec, size_t elsz, int32_t len) {
  uint64_t need;
  if(len < 0)
    return;
  if(!umult64(elsz, len, &need))
    return;
  if(need > vec->size)
    return;
  vec->size = need;
}

void
vector_put(Vector* vec, const void* bytes, size_t len) {
  size_t pos;
  if(!len)
    return;
  pos = vec->size;
  if(!vector_allocate(vec, 1, vec->size + len - 1))
    return;
  memcpy(vec->data + pos, bytes, len);
}

void __attribute__((format(printf, 2, 3))) vector_printf(Vector* vec, const char* fmt, ...) {
  va_list ap;
  char buf[128];
  size_t len;
  va_start(ap, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if(len < sizeof(buf)) {
    vector_put(vec, buf, len);
  } else {
    size_t pos = vec->size;
    if(!vector_allocate(vec, 1, vec->size + len))
      return;
    va_start(ap, fmt);
    len = vsnprintf((char*)(vec->data + pos), len, fmt, ap);
    va_end(ap);
    vec->size += len;
  }
}

void
vector_diff(void* a, size_t m, void* b, size_t n, size_t elsz, Vector* out) {
  char* ptr = a;
  size_t i;
  for(i = 0; i < m; i++) {
    if(array_contains(b, n, elsz, ptr))
      vector_put(out, ptr, elsz);
    ptr += elsz;
  }
}

void
vector_symmetricdiff(
    void* a, size_t m, void* b, size_t n, size_t elsz, Vector* out_a, Vector* out_b) {
  vector_diff(a, m, b, n, elsz, out_a);
  vector_diff(b, n, a, m, elsz, out_b);
}

void
vector_intersection(void* a, size_t m, void* b, size_t n, size_t elsz, Vector* out) {
  size_t i, j = 0, k = 0;
  for(i = 0; i < m + n; i++) {
    void* aptr = (char*)a + j * elsz;
    void* bptr = (char*)b + k * elsz;
    int r = memcmp(aptr, bptr, elsz);
    if(r < 0 && j < m) {
      j++;
    } else if(r > 0 && k < n) {
      k++;
    } else if(r == 0 && j < m && k < n) {
      vector_put(out, aptr, elsz);
      j++;
      k++;
    }
  }
}

ssize_t
vector_find(Vector* vec, size_t elsz, const void* ptr) {
  size_t i, j;
  for(i = 0, j = 0; i < vec->size; i += elsz) {
    if(!memcmp(&vec->data[i], ptr, elsz))
      return j;

    j++;
  }
  return -1;
}

int
vector_copy(Vector* dst, const Vector* src) {
  dst->realloc_func = src->realloc_func;
  dst->opaque = src->opaque;
  dst->data = 0;
  dst->capacity = 0;
  if(!dbuf_realloc(&dst->dbuf, src->size)) {
    memcpy(dst->data, src->data, src->size);
    return 1;
  }
  return 0;
}
