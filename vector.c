#include "vector.h"
#include "buffer-utils.h"
#include "utils.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HAVE_UINT128

#if(defined(__GNUC__) && (__GNUC__ >= 5)) || defined(HAVE__BUILTIN_MUL_OVERFLOW)

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

void
vector_free(Vector* vec) {
  if(vec->data)
    vec->realloc_func(vec->opaque, vec->data, 0);
  vec->data = 0;
  vec->capacity = vec->size = 0;
}

int32_t
vector_indexof(const Vector* vec, size_t elsz, void* ptr) {
  if(ptr < vector_begin(vec) || ptr > vector_back(vec, elsz))
    return -1;

  return ((size_t)vector_begin(vec) - (size_t)ptr) / elsz;
}

int32_t
vector_find(const Vector* vec, size_t elsz, void* ptr) {
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

int32_t
vector_finds(const Vector* vec, const char* str) {
  char** x;
  int32_t i = 0;
  if(vector_empty(vec))
    return -1;

  vector_foreach_t(vec, x) {
    if(!strcmp(*x, str))
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

  vector_foreach_t(vec, x) {
    if(!strcmp(*x, str))
      ++count;
  }
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
  memcpy(vec->data + pos, bytes, len);
  return vec->data + pos;
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
/*
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
*/
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

void
vector_fwrite(const Vector* vec, size_t start, FILE* out) {
  size_t i, len = vector_size(vec, sizeof(char*));
  for(i = start; i < len; i++) {
    const char* str = *(char**)vector_at(vec, sizeof(char*), i);
    fputs(i > start ? "',\n  '" : "[\n  '", out);
    fputs(str, out);
    if(i + 1 == len)
      fputs("'\n]", out);
  }
  fflush(out);
}
