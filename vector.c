#include "quickjs.h"
#include "vector.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
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
    if(need >= vec->capacity) {
      if(elsz < 8)
        need = (need + 127) & (~127);
      else
        need = (need + 4095) & (~4095);
      assert(need >= 128);
      if(!(tmp = realloc(vec->data, need)))
        return 0;
      vec->data = tmp;
      vec->capacity = need;
      memset(vec->data + vec->size, 0, vec->capacity - vec->size);
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
