#ifndef VECTOR_H
#define VECTOR_H

#include "quickjs.h"
#include "cutils.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>

#define roundto(n, mod) (((n) = (((n) + (mod)-1))), n = (n) - ((uint64_t)(n) % (uint64_t)(mod)))

typedef union Vector {
  DynBuf dbuf;
  struct {
    char* data;
    size_t size;
    size_t capacity;
    BOOL error;
    DynBufReallocFunc* realloc_func;
    void* opaque;
  };
} Vector;

#define VECTOR_INIT()                                                                                                  \
  {                                                                                                                    \
    { 0, 0, 0, 0, &vector_default_realloc, 0 }                                                                         \
  }

#define vector_init(vec, ctx) js_dbuf_init(ctx, &((vec)->dbuf))
#define vector_init_rt(vec, rt) js_dbuf_init_rt(rt, &((vec)->dbuf))
#define VECTOR(ctx)                                                                                                    \
  (Vector) {                                                                                                           \
    { 0, 0, 0, 0, (DynBufReallocFunc*)&js_realloc, ctx }                                                               \
  }
#define VECTOR_RT(rt)                                                                                                  \
  (Vector) {                                                                                                           \
    { 0, 0, 0, 0, (DynBufReallocFunc*)&js_realloc_rt, rt }                                                             \
  }
#define vector_begin(vec) ((void*)((vec)->data))
#define vector_end(vec) ((void*)((vec)->data + (vec)->size))

#define vector_foreach_t(a, p) for((p) = vector_begin(a); (p) != vector_end(a); ++(p))
#define vector_foreach(a, msz, p)                                                                                      \
  for((p) = vector_begin(a); (char*)(p) != (char*)vector_end(a); (p) = (void*)(((char*)p) + msz))

#if(defined(__GNUC__) && (__GNUC__ >= 5)) || defined(HAVE__BUILTIN_MUL_OVERFLOW)
static inline int
umult64(uint64_t a, uint64_t b, uint64_t* c) {
  return !__builtin_mul_overflow(a, b, c);
}
#elif defined(HAVE_UINT128)
static inline int
umult64(uint64_t a, uint64_t b, uint64_t* c) {
  __uint128_t x = ((__uint128_t)a) * b;
  if((*c = (uint64_t)x) != x)
    return 0;
  return 1;
}
#else
extern int umult64(uint64_t a, uint64_t b, uint64_t* c);
#endif

int32_t vector_indexof(const Vector* vec, size_t elsz, void* ptr);
int32_t vector_find(const Vector* vec, size_t elsz, void* ptr);
int32_t vector_finds(const Vector* vec, const char* str);
void* vector_put(Vector* vec, const void* bytes, size_t len);
void vector_free(Vector* vec);
void vector_printf(Vector* vec, const char*, ...);
void vector_intersection(void*, size_t, void*, size_t, size_t, Vector*);
void vector_diff(void*, size_t, void*, size_t, size_t, Vector*);
void vector_symmetricdiff(void*, size_t, void*, size_t, size_t, Vector*, Vector*);
int vector_copy(Vector* dst, const Vector* src);
void vector_fwrite(const Vector*, size_t, FILE* out);

#define vector_push(vec, elem) vector_put((vec), &(elem), sizeof((elem)))

static inline void*
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
    }
    vec->size = ((uint32_t)pos + 1) * elsz;
  }
  return vec->data + (uint32_t)pos * elsz;
}

static inline BOOL
vector_shrink(Vector* vec, size_t elsz, int32_t len) {
  uint64_t need;
  if(len < 0)
    return FALSE;
  if(!umult64(elsz, len, &need))
    return FALSE;
  if(need > vec->size)
    return FALSE;
  vec->size = need;
  return TRUE;
}

static inline BOOL
vector_grow(Vector* vec, size_t elsz, int32_t len) {
  uint64_t need;
  if(len < 0)
    return FALSE;
  if(!umult64(elsz, len, &need))
    return FALSE;
  if(need <= vec->size)
    return FALSE;
  vec->data = realloc(vec->data, need);
  vec->size = need;
  return TRUE;
}

static inline void*
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

static inline void*
vector_default_realloc(void* opaque, void* ptr, size_t size) {
  return realloc(ptr, size);
}

static inline uint32_t
vector_size(const Vector* vec, size_t elsz) {
  return vec->size / elsz;
}

static inline int
vector_empty(const Vector* vec) {
  return vec->size == 0;
}

static inline void*
vector_front(const Vector* vec, size_t elsz) {
  assert(vec->size >= elsz);
  return vec->data;
}

static inline void*
vector_back(const Vector* vec, size_t elsz) {
  uint32_t n = vector_size(vec, elsz);
  assert(n);
  return vector_at(vec, elsz, n - 1);
}

static inline void
vector_clear(Vector* vec) {
  vec->size = 0;
}

static inline void*
vector_emplace(Vector* vec, size_t elsz) {
  uint32_t n = vector_size(vec, elsz);
  return vector_allocate(vec, elsz, n);
}

static inline void*
vector_pop(Vector* vec, size_t elsz) {
  uint32_t n = vector_size(vec, elsz);
  assert(n);
  vector_shrink(vec, elsz, n - 1);
  return vector_end(vec);
}

static inline void
vector_puts(Vector* vec, const char* str) {
  vector_put(vec, str, strlen(str));
}

static inline void
vector_putc(Vector* vec, char c) {
  vector_put(vec, &c, 1);
}

static inline void
vector_put0(Vector* vec) {
  vector_put(vec, "\0", 1);
}

static inline void
vector_putlong(Vector* vec, long l, int radix) {
  char buf[64];
  size_t len = snprintf(buf, sizeof(buf), radix == 16 ? "%lx" : radix == 8 ? "%lo" : "%lu", l);
  vector_put(vec, buf, len);
}

static inline void
vector_putptr(Vector* vec, void* p) {
  vector_put(vec, &p, sizeof(p));
}

static inline void
vector_sort(Vector* vec, size_t elsz, int (*compar)(const void*, const void*, void*), void* arg) {
  qsort_r(vector_begin(vec), vector_size(vec, elsz), elsz, compar, arg);
}

static inline void
vector_catlong(Vector* vec, long l, int radix) {
  char buf[64];
  size_t len = snprintf(buf, sizeof(buf), radix == 16 ? "%lx" : radix == 8 ? "%lo" : "%lu", l);
  vector_put(vec, buf, len);
}

#endif /* defined(VECTOR_H) */
