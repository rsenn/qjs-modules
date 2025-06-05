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
  MemoryBlock block;
  Vector vec;
} Blob;

extern VISIBLE JSClassID js_blob_class_id;

VISIBLE JSValue js_blob_wrap(JSContext*, Blob* blob);
VISIBLE JSValue js_blob_new(JSContext*, const void* x, size_t len, const char* type);

static inline Blob*
js_blob_data(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque(value, js_blob_class_id);
}

/**
 * @}
 */

#endif /* defined(QUICKJS_BLOB_H) */
