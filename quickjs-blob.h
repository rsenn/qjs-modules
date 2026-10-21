#ifndef QUICKJS_BLOB_H
#define QUICKJS_BLOB_H

#include "defines.h"
#include "vector.h"

/**
 * \defgroup quickjs-blob quickjs-blob: Blob
 * @{
 */
/* a Blob: an immutable list of segments, one InputBuffer each.
 *
 *   segment     value       data
 *   owning      undefined   the Blob's own copy, freed with the Blob
 *   non-owning  an object   lives in value, the segment holds one ref
 *
 * a segment's bytes are inputbuffer_data(seg), inputbuffer_length(seg).
 */
typedef struct blob {
  Vector segments; /* InputBuffer per segment, js_realloc_rt */
  size_t size;     /* sum of the segment lengths */
  char* type;
} Blob;

extern VISIBLE JSClassID js_blob_class_id;

VISIBLE JSValue js_blob_wrap(JSContext*, Blob* blob);
VISIBLE JSValue js_blob_new(JSContext*, const void* x, size_t len, const char* type);

static inline Blob*
js_blob_data2(JSContext* ctx, JSValueConst value) {
  return JS_GetOpaque2(ctx, value, js_blob_class_id);
}

/**
 * @}
 */

#endif /* defined(QUICKJS_BLOB_H) */
