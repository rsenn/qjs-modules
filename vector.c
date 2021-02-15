#include "quickjs.h"
#include "vector.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

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

void*
vector_allocate(vector* vec, size_t elsz, int32_t pos) {
  uint64_t need;
  void* tmp;
  if(pos < 0)
    return 0;
  if(!umult64(elsz, pos + 1, &need))
    return 0;

  if(need > vec->size) {
    if(need > vec->capacity) {
      if(elsz < 8)
        need = (need + 1023) & (~1023);
      else
        need = (need + 4095) & (~4095);
      assert(need >= 512);
      if(!(tmp = realloc(vec->data, need)))
        return 0;
      vec->data = tmp;
      if(need > vec->capacity)
        memset(vec->data + vec->capacity, 0, need - vec->capacity);
      vec->capacity += need;
    }
    vec->size = ((uint32_t)pos + 1) * elsz;
  }
  return vec->data + (uint32_t)pos * elsz;
}

void
vector_free(vector* vec) {
  if(vec->data)
    free(vec->data);
  vec->data = 0;
  vec->capacity = vec->size = 0;
}

void*
vector_at(const vector* vec, size_t elsz, int32_t pos) {
  uint64_t offs;
  if(pos < 0)
    return 0;
  if(!umult64(elsz, pos, &offs))
    return 0;
  if(offs >= vec->size)
    return 0;
  return vec->data + offs;
}

void
vector_shrink(vector* vec, size_t elsz, int32_t len) {
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
vector_catb(vector* vec, const void* bytes, size_t len) {
  size_t pos;
  if(!len)
    return;
  pos = vec->size;
  if(!vector_allocate(vec, 1, vec->size + len - 1))
    return;
  memcpy(vec->data + pos, bytes, len);
}

void __attribute__((format(printf, 2, 3))) vector_printf(vector* vec, const char* fmt, ...) {
  va_list ap;
  char buf[128];
  int len;
  va_start(ap, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if(len < sizeof(buf)) {
    vector_catb(vec, buf, len);
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
