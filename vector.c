#include "vector.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void*
vector_allocate(vector* vec, size_t elsz, ssize_t pos) {
  size_t need;
  void* tmp;
  if((need = elsz * (pos + 1)) > vec->size) {
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
    vec->size = (pos + 1) * elsz;
  }
  return vec->data + pos * elsz;
}

void
vector_free(vector* vec) {
  if(vec->data)
    free(vec->data);
  vec->data = 0;
  vec->capacity = vec->size = 0;
}

void*
vector_at(const vector* vec, size_t elsz, ssize_t pos) {
  size_t offs;
  if(pos < 0 || (offs = elsz * pos) >= vec->size)
    return 0;
  return vec->data + offs;
}

void
vector_shrink(vector* vec, size_t elsz, ssize_t len) {
  size_t need;
  if((len < 0))
    return;
  if((need = elsz * len) > vec->size)
    return;
  vec->size = need;
}