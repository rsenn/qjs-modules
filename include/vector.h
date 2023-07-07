#ifndef VECTOR_H
#define VECTOR_H

#include <quickjs.h>
#include <cutils.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>

#include "debug.h"

/**
 * \defgroup vector vector: Vector implementation
 * @{
 */
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

#define VECTOR_INIT() \
  { \
    { 0, 0, 0, 0, &vector_realloc, 0 } \
  }

#define vector_init(vec, ctx) dbuf_init2(&((vec)->dbuf), (ctx), (DynBufReallocFunc*)&vector_js_realloc)
#define vector_init_rt(vec, rt) dbuf_init2(&((vec)->dbuf), (rt), (DynBufReallocFunc*)&vector_js_realloc_rt)
#define VECTOR(ctx) \
  (Vector) { \
    { 0, 0, 0, 0, (DynBufReallocFunc*)&vector_js_realloc, ctx } \
  }
#define VECTOR_RT(rt) \
  (Vector) { \
    { 0, 0, 0, 0, (DynBufReallocFunc*)&vector_js_realloc_rt, rt } \
  }

#define vector_begin(vec) ((void*)((vec)->data))
#define vector_end(vec) ((void*)((vec)->data + (vec)->size))

#define vector_begin_t(vec, t) ((t*)vector_begin(vec))
#define vector_end_t(vec, t) ((t*)vector_end(vec))

#define vector_foreach_t(a, p) for((p) = vector_begin(a); (p) != vector_end(a); ++(p))
#define vector_foreach(a, msz, p) for((p) = vector_begin(a); (char*)(p) != (char*)vector_end(a); (p) = (void*)(((char*)p) + msz))

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

void* vector_realloc(void*, void* ptr, size_t size);
void* vector_js_realloc(JSContext* ctx, void* ptr, size_t size);
void* vector_js_realloc_rt(JSRuntime* rt, void* ptr, size_t size);
int32_t vector_indexof(const Vector* vec, size_t elsz, void* ptr);
int32_t vector_find(const Vector* vec, size_t elsz, void* ptr);
int32_t vector_finds(const Vector* vec, const char* str);
int vector_counts(const Vector* vec, const char* str);
void* vector_put(Vector* vec, const void* bytes, size_t len);
void vector_free(Vector* vec);
void vector_printf(Vector* vec, const char*, ...);
void vector_intersection(void*, size_t, void*, size_t, size_t, Vector*);
void vector_diff(void*, size_t, void*, size_t, size_t, Vector*);
void vector_symmetricdiff(void*, size_t, void*, size_t, size_t, Vector*, Vector*);
int vector_copy(Vector* dst, const Vector* src);
void vector_fwrite(const Vector*, size_t, FILE* out);
BOOL vector_grow(Vector* vec, size_t elsz, int32_t len);
char* vector_pushstring(Vector*, const char*);
char* vector_pushstringlen(Vector*, const char*, size_t);
void vector_clearstrings(Vector*);
void vector_dumpstrings(const Vector*, DynBuf* buf);

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

static inline void
vector_freestrings(Vector* vec) {
  vector_clearstrings(vec);
  vector_free(vec);
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

void quicksort_r(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*, void*), void* ptr);

static inline void
vector_sort(Vector* vec, size_t elsz, int (*compar)(const void*, const void*, void*), void* arg) {
  quicksort_r(vector_begin(vec), vector_size(vec, elsz), elsz, compar, arg);
}

static inline void
vector_catlong(Vector* vec, long l, int radix) {
  char buf[64];
  size_t len = snprintf(buf, sizeof(buf), radix == 16 ? "%lx" : radix == 8 ? "%lo" : "%lu", l);
  vector_put(vec, buf, len);
}

/**
 * @}
 */
#endif /* defined(VECTOR_H) */
