#ifndef VECTOR_H
#define VECTOR_H

#include "quickjs.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
  char* data;
  size_t size;
  size_t capacity;
} vector;

#define VECTOR_INIT()                                                                                      \
  { 0, 0, 0 }

#define vector_init(vec) memset((vec), 0, sizeof(vector))
#define vector_foreach_t(a, p) for((p) = vector_begin(a); (char*)(p) < (char*)vector_end(a); ++(p))
#define vector_foreach(a, msz, p)                                                                          \
  for((p) = vector_begin(a); (char*)(p) < (char*)vector_end(a); (p) = (void*)(((char*)p) + msz))

int umult64(uint64_t a, uint64_t b, uint64_t* c);
void* vector_allocate(vector* vec, size_t elsz, int32_t pos);
void* vector_at(const vector* vec, size_t elsz, int32_t pos);
void vector_put(vector* vec, const void* bytes, size_t len);
void vector_free(vector* vec);
void vector_shrink(vector* vec, size_t elsz, int32_t len);
void vector_printf(vector* vec, const char*, ...);

void  vector_intersection(void*, size_t, void*, size_t, size_t, vector*);
void  vector_diff(void*, size_t, void*, size_t, size_t, vector*);
void  vector_symmetricdiff(void*, size_t, void*, size_t, size_t, vector*, vector*);

#define vector_push(vec, elem) vector_put((vec), &(elem), sizeof((elem)))

static inline uint32_t
vector_size(const vector* vec, size_t elsz) {
  return vec->size / elsz;
}

static inline int
vector_empty(const vector* vec) {
  return vec->size == 0;
}

static inline void*
vector_begin(const vector* vec) {
  return vec->data;
}

static inline void*
vector_end(const vector* vec) {
  return vec->data + vec->size;
}

static inline void*
vector_front(const vector* vec, size_t elsz) {
  assert(vec->size >= elsz);
  return vec->data;
}

static inline void*
vector_back(const vector* vec, size_t elsz) {
  uint32_t n = vector_size(vec, elsz);
  assert(n);
  return vector_at(vec, elsz, n - 1);
}

static inline void
vector_clear(vector* vec) {
  vec->size = 0;
}

static inline void*
vector_emplace(vector* vec, size_t elsz) {
  uint32_t n = vector_size(vec, elsz);
  return vector_allocate(vec, elsz, n);
}

static inline void*
vector_pop(vector* vec, size_t elsz) {
  uint32_t n = vector_size(vec, elsz);
  assert(n);
  vector_shrink(vec, elsz, n - 1);
  return vector_end(vec);
}

static inline void
vector_puts(vector* vec, const char* str) {
  vector_put(vec, str, strlen(str));
}

static inline void
vector_putc(vector* vec, char c) {
  vector_put(vec, &c, 1);
}

static inline void
vector_put0(vector* vec) {
  vector_put(vec, "\0", 1);
}

static inline void
vector_putlong(vector* vec, long l, int radix) {
  char buf[64];
  size_t len = snprintf(buf, sizeof(buf), radix == 16 ? "%lx" : radix == 8 ? "%lo" : "%lu", l);
  vector_put(vec, buf, len);
}

static inline void
vector_sort(vector* vec, size_t elsz, int (*compar)(const void*, const void*, void*), void* arg) {
  qsort_r(vector_begin(vec), vector_size(vec, elsz), elsz, compar, arg);
}

static inline void
vector_catlong(vector* vec, long l, int radix) {
  char buf[64];
  size_t len = snprintf(buf, sizeof(buf), radix == 16 ? "%lx" : radix == 8 ? "%lo" : "%lu", l);
  vector_put(vec, buf, len);
}

#endif /* defined(VECTOR_H) */
