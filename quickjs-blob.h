#ifndef QUICKJS_BLOB_H
#define QUICKJS_BLOB_H

#include "utils.h"
#include "vector.h"
#include "buffer-utils.h"

/**
 * \defgroup quickjs-blob quickjs-blob: Blob
 * @{
 */
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

extern VISIBLE JSClassID js_blob_class_id;
extern VISIBLE JSValue blob_proto, blob_ctor;

Blob* blob_new(JSContext*, const void* x, size_t len, const char* type);
ssize_t blob_write(JSContext*, Blob* blob, const void* x, size_t len);
void blob_free(JSRuntime*, Blob* blob);
InputBuffer blob_input(JSContext*, Blob* blob);

VISIBLE JSValue js_blob_wrap(JSContext*, Blob* blob);
VISIBLE JSValue js_blob_new(JSContext*, const void* x, size_t len, const char* type);

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

/**
 * @}
 */

#endif /* defined(QUICKJS_BLOB_H) */
