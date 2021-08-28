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

Blob* js_blob_data(JSContext* ctx, JSValue value);
JSValue js_blob_constructor(JSContext* ctx, JSValue new_target, int argc, JSValue argv[]);
void js_blob_finalizer(JSRuntime* rt, JSValue val);
int js_blob_init(JSContext* ctx, JSModuleDef* m);
JSModuleDef* js_init_module_blob(JSContext* ctx, const char* module_name);

#endif /* defined(QUICKJS_BLOB_H) */
