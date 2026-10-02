#include "vector.h"
#include "buffer-utils.h"
#include "utils.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"

/**
 * \addtogroup vector
 * @{
 */

realloc2_helper(vector_realloc);
js_realloc_helper(vector_js_realloc);
js_realloc_rt_helper(vector_js_realloc_rt);

#define HAVE_UINT128

#if (defined(__GNUC__) && (__GNUC__ >= 5)) || defined(HAVE__BUILTIN_MUL_OVERFLOW)

/*#elif defined(HAVE_UINT128)
#warning No umult64 implementation*/
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

  if(a <= 0xffffffff) {
    uint64_t x = (uint64_t)(alo)*blo;
    if(x + (a << 32) < x)
      return 0;
    *c = x + (a << 32);
    return 1;
  }

  return 0;
}
#endif

void
vector_free(Vector* vec) {
  if(vec->buf)
    vec->realloc_func(vec->opaque, vec->buf, 0);

  vec->buf = 0;
  vec->allocated_size = vec->size = 0;
}

int32_t
vector_find(const Vector* vec, size_t elsz, const void* ptr) {
  void* x;
  int32_t i = 0;

  if(vector_empty(vec))
    return -1;

  vector_foreach(vec, elsz, x) {
    if(!memcmp(x, ptr, elsz))
      return i;

    i++;
  }

  return -1;
}

int
vector_counts(const Vector* vec, const char* str) {
  char** x;
  int count = 0;

  if(vector_empty(vec))
    return 0;

  vector_foreach_t(vec, x) if(!strcmp(*x, str))++ count;

  return count;
}

void*
vector_put(Vector* vec, const void* bytes, size_t len) {
  size_t pos;

  if(!len)
    return 0;

  pos = vec->size;

  if(!vector_allocate(vec, 1, vec->size + len - 1))
    return 0;

  memcpy(vec->buf + pos, bytes, len);

  return vec->buf + pos;
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

int
vector_copy(Vector* dst, const Vector* src) {
  dst->realloc_func = src->realloc_func;
  dst->opaque = src->opaque;
  dst->buf = 0;
  dst->allocated_size = 0;
  dst->size = 0;

  if(!dbuf_claim(dst, src->size - dst->size)) {
    memcpy(dst->buf, src->buf, src->size);
    dst->size = src->size;
    return 1;
  }

  return 0;
}

char*
vector_pushstringlen(Vector* vec, const char* str, size_t len) {
  char* s;

  if((s = vec->realloc_func(vec->opaque, 0, len + 1))) {
    strncpy(s, str, len);
    s[len] = '\0';
    vector_push(vec, s);
  }

  return s;
}

void
vector_clearstrings(Vector* vec) {
  char** ptr;

  vector_foreach_t(vec, ptr) free(*ptr);
  vector_clear(vec);
}

void*
vector_ready(Vector* vec, size_t n) {
  size_t a = vec->allocated_size;

  if(n > a) {
    if(dbuf_claim(vec, n - vec->size))
      return 0;

    if(vec->allocated_size > a)
      memset(vec->buf + a, 0, vec->allocated_size - a);
  }

  return vec->buf;
}

void*
vector_readyplus(Vector* vec, size_t need) {
  uint8_t* ptr;

  if(!(ptr = vector_ready(vec, vec->size + need)))
    return 0;

  return ptr + vec->size;
}

/**
 * @}
 */
