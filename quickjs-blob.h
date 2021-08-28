#ifndef QUICKJS_BLOB_H
#define QUICKJS_BLOB_H

#include <threads.h>
#include "vector.h"
#include "utils.h"
#include "buffer-utils.h"

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
extern thread_local JSValue blob_proto, blob_ctor;

Blob* blob_new(JSContext*, const void* x, size_t len, const char* type);
ssize_t blob_write(JSContext*, Blob* blob, const void* x, size_t len);
void blob_free(JSContext*, Blob* blob);
InputBuffer blob_input(JSContext*, Blob* blob);
JSValue js_blob_wrap(JSContext*, Blob* blob);
JSValue js_blob_new(JSContext*, const void* x, size_t len, const char* type);
int js_blob_init(JSContext*, JSModuleDef* m);

static inline void*
blob_data(Blob* blob) {
  return blob->data;
}

static inline size_t
blob_size(Blob* blob) {
  return blob->size;
}

static inline Blob*
js_blob_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_blob_class_id);
}

#endif /* defined(QUICKJS_BLOB_H) */
