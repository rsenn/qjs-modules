#ifndef VECTOR_H
#define VECTOR_H

#include <assert.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
  char* data;
  size_t size;
  size_t capacity;
} vector;

#define VECTOR_INIT()                                                                                                  \
  { 0, 0, 0 }

#define vector_init(vec) memset((vec), 0, sizeof(vector))
#define vector_foreach_t(a, p) for((p) = vector_begin(a); (char*)(p) < (char*)vector_end(a); ++(p))
#define vector_foreach(a, msz, p)                                                                                      \
  for((p) = vector_begin(a); (char*)(p) < (char*)vector_end(a); (p) = (void*)(((char*)p) + msz))

int umult64(uint64_t a, uint64_t b, uint64_t* c);
void* vector_allocate(vector* vec, size_t elsz, int32_t pos);
void* vector_at(const vector* vec, size_t elsz, int32_t pos);
void vector_catb(vector* vec, const void* bytes, size_t len);
void vector_free(vector* vec);
void vector_shrink(vector* vec, size_t elsz, int32_t len);
void vector_printf(vector* vec, const char*, ...);

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
vector_push(vector* vec, size_t elsz) {
  uint32_t n = vector_size(vec, elsz);
  return vector_allocate(vec, elsz, n);
}

static inline void
vector_pop(vector* vec, size_t elsz) {
  uint32_t n = vector_size(vec, elsz);
  assert(n);
  return vector_shrink(vec, elsz, n - 1);
}

static inline void
vector_cats(vector* vec, const char* str) {
  vector_catb(vec, str, strlen(str));
}

static inline void
vector_cat0(vector* vec) {
  vector_catb(vec, "\0", 1);
}
static inline void
vector_catlong(vector* vec, long l, int radix) {
  char buf[64];
  size_t len = snprintf(buf, sizeof(buf), radix == 16 ? "%lx" : radix == 8 ? "%lo" : "%lu", l);
  vector_catb(vec, buf, len);
}

#endif /* VECTOR_H */
