#ifndef VECTOR_H
#define VECTOR_H

#include <assert.h>
#include <sys/types.h>

typedef struct {
  char* data;
  size_t size;
  size_t capacity;
} vector;

#define vector_init(vec) memset((vec), 0, sizeof(vector))

void* vector_allocate(vector* vec, size_t elsz, ssize_t pos);
void vector_free(vector* vec);
void* vector_at(const vector* vec, size_t elsz, ssize_t pos);
void vector_shrink(vector* vec, size_t elsz, ssize_t len);

static inline size_t
vector_size(const vector* vec, size_t elsz) {
  return vec->size / elsz;
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
	assert (vec->size >= elsz);
  return vec->data;
}

static inline void*
vector_back(const vector* vec, size_t elsz) {
		size_t n = vector_size(vec, elsz);
assert (n);
  return vec->data + (n - 1) * elsz;
}

static inline void
vector_clear(vector* vec) {
  vec->size = 0;
}

static inline void*
vector_push(vector* vec, size_t elsz) {
	size_t n = vector_size(vec, elsz);
	return vector_allocate(vec, elsz, n);
}

static inline void
vector_pop(vector* vec, size_t elsz) {
	size_t n = vector_size(vec, elsz);
	assert(n);
	return vector_shrink(vec, elsz, n - 1);
}

#endif /* VECTOR_H */