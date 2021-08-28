#ifndef QUICKJS_BLOB_H
#define QUICKJS_BLOB_H

#include <threads.h>
#include "vector.h"

typedef union blob {
  struct {
    uint8_t* data;
    size_t size, capacity;
    BOOL error;
    DynBufReallocFunc* realloc_func;
    void* opaque;
    char* type;
  };
  Vector vec;
} Blob;

extern thread_local JSClassID js_blob_class_id;

Blob* js_blob_data(JSContext*, JSValue value);
JSValue js_blob_wrap(JSContext*, Blob* blob);
JSValue js_blob_new(JSContext*, const void* x, size_t len, const char* type);
JSValue js_blob_constructor(JSContext*, JSValue new_target, int argc, JSValue argv[]);
void js_blob_finalizer(JSRuntime*, JSValue val);
int js_blob_init(JSContext*, JSModuleDef* m);

static inline void*
blob_data(Blob* blob) {
  return blob->data;
}

static inline size_t
blob_size(Blob* blob) {
  return blob->size;
}

#endif /* defined(QUICKJS_BLOB_H) */
