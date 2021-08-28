#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "quickjs-blob.h"
#include "utils.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

thread_local VISIBLE JSClassID js_blob_class_id = 0;
thread_local JSValue blob_proto = {JS_TAG_UNDEFINED}, blob_ctor = {JS_TAG_UNDEFINED};

enum { BLOB_SIZE, BLOB_TYPE };
enum { BLOB_ARRAYBUFFER, BLOB_SLICE, BLOB_STREAM, BLOB_TEXT };

static Blob*
blob_new(JSContext* ctx, const void* x, size_t len, const char* type) {
  Blob* blob;

  if(!(blob = js_mallocz(ctx, sizeof(Blob))))
    return 0;

  blob->vec = VECTOR(ctx);
  if(x) {
    vector_allocate(&blob->vec, 1, len - 1);
    memcpy(blob->vec.data, x, len);
  }

  blob->type = js_strdup(ctx, type);
  return blob;
}

static void
js_blob_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  // js_free_rt(rt, ptr);
}

Blob*
js_blob_data(JSContext* ctx, JSValueConst value) {
  Blob* blob;
  blob = JS_GetOpaque(value, js_blob_class_id);
  return blob;
}

JSValue
js_blob_wrap(JSContext* ctx, Blob* blob) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, blob_proto, js_blob_class_id);
  JS_SetOpaque(obj, blob);
  return obj;
}

JSValue
js_blob_new(JSContext* ctx, const void* x, size_t len, const char* type) {
  Blob* blob;

  if(!(blob = blob_new(ctx, x, len, type)))
    return JS_ThrowOutOfMemory(ctx);

  return js_blob_wrap(ctx, blob);
}

static JSValue
js_blob_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Blob* blob;
  JSValue ret = JS_UNDEFINED;
  if(!(blob = js_blob_data(ctx, this_val)))
    return ret;
  switch(magic) {
    case BLOB_SIZE: {
      ret = JS_NewUint32(ctx, blob->size);
      break;
    }

    case BLOB_TYPE: {
      ret = JS_NewString(ctx, blob->type);
      break;
    }
  }
  return ret;
}
/*
static JSValue
js_blob_new(JSContext* ctx, const void* x, size_t len, const char* type) {
  Blob* blob = blob_new(ctx, x, len, type);
  return js_blob_wrap(ctx, blob);
}
*/
static JSValue
js_blob_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  Blob* blob;
  JSValue ret = JS_UNDEFINED;
  if(!(blob = js_blob_data(ctx, this_val)))
    return ret;
  switch(magic) {}
  return ret;
}

JSValue
js_blob_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  Blob* blob;

  if(!(blob = js_mallocz(ctx, sizeof(Blob))))
    return JS_ThrowOutOfMemory(ctx);

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;
  if(!JS_IsObject(proto))
    proto = blob_proto;

  /* using new_target to get the prototype is necessary when the
     class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_blob_class_id);
  if(JS_IsException(obj))
    goto fail;

  blob->vec = VECTOR(ctx);

  {
    uint8_t* ptr;
    size_t byte_length = 0, byte_offset = 0;
    blob->type = 0;

    if(argc >= 1) {
      uint32_t i, len = js_array_length(ctx, argv[0]);
      InputBuffer* inputs = js_malloc(ctx, sizeof(InputBuffer) * len);

      for(i = 0; i < len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        inputs[i] = js_input_buffer(ctx, item);

        byte_length += inputs[i].size;
        //        vector_put(&blob->vec, in.data, in.size);

        //      input_buffer_free(&in, ctx);
        JS_FreeValue(ctx, item);
      }

      vector_allocate(&blob->vec, 1, byte_length - 1);

      ptr = vector_begin(&blob->vec);
      for(i = 0; i < len; i++) {
        memcpy(ptr + byte_offset, inputs[i].data, inputs[i].size);
        byte_offset += inputs[i].size;
      }
    }

    if(argc >= 2 && JS_IsObject(argv[1])) {
      JSValue type = JS_GetPropertyStr(ctx, argv[1], "type");

      if(JS_IsString(type))
        blob->type = js_tostring(ctx, type);
      JS_FreeValue(ctx, type);
    }
    if(blob->type == 0)
      blob->type = js_strdup(ctx, "application/binary");
  }

  JS_SetOpaque(obj, blob);

  return obj;

fail:
  js_free(ctx, blob);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

static JSValue
js_blob_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Blob* blob;
  JSValue ret = JS_UNDEFINED;

  if(!(blob = js_blob_data(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case BLOB_ARRAYBUFFER: {
      ret = js_arraybuffer_fromvalue(ctx, blob->data, blob->size, this_val);
      break;
    }
    case BLOB_TEXT: {
      ret = JS_NewStringLen(ctx, blob->data, blob->size);
      break;
    }
  }
  return ret;
}

void
js_blob_finalizer(JSRuntime* rt, JSValue val) {
  Blob* blob = JS_GetOpaque(val, js_blob_class_id);
  if(blob) {
    vector_free(&blob->vec);

    js_free_rt(rt, blob->type);
    js_free_rt(rt, blob);
  }
  JS_FreeValueRT(rt, val);
}

static JSClassDef js_blob_class = {
    .class_name = "Blob",
    .finalizer = js_blob_finalizer,
};

static const JSCFunctionListEntry js_blob_funcs[] = {
    JS_CFUNC_MAGIC_DEF("arrayBuffer", 0, js_blob_method, BLOB_ARRAYBUFFER),
    JS_CFUNC_MAGIC_DEF("stream", 0, js_blob_method, BLOB_STREAM),
    JS_CFUNC_MAGIC_DEF("slice", 0, js_blob_method, BLOB_SLICE),
    JS_CFUNC_MAGIC_DEF("text", 0, js_blob_method, BLOB_TEXT),
    JS_CGETSET_MAGIC_DEF("size", js_blob_get, 0, BLOB_SIZE),
    JS_CGETSET_MAGIC_DEF("type", js_blob_get, 0, BLOB_TYPE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Blob", JS_PROP_CONFIGURABLE),
};

int
js_blob_init(JSContext* ctx, JSModuleDef* m) {

  if(js_blob_class_id == 0) {
    JS_NewClassID(&js_blob_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_blob_class_id, &js_blob_class);

    blob_ctor = JS_NewCFunction2(ctx, js_blob_constructor, "Blob", 1, JS_CFUNC_constructor, 0);
    blob_proto = JS_NewObject(ctx);

    JS_SetPropertyFunctionList(ctx, blob_proto, js_blob_funcs, countof(js_blob_funcs));
    JS_SetClassProto(ctx, js_blob_class_id, blob_proto);

    // js_set_inspect_method(ctx, blob_proto, js_blob_inspect);
  }

  if(m) {
    JS_SetModuleExport(ctx, m, "Blob", blob_ctor);

    const char* module_name = JS_AtomToCString(ctx, m->module_name);

    if(!strcmp(module_name, "blob"))
      JS_SetModuleExport(ctx, m, "default", blob_ctor);

    JS_FreeCString(ctx, module_name);
  }

  return 0;
}

#ifdef JS_BLOB_MODULE
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_blob
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  if(!(m = JS_NewCModule(ctx, module_name, &js_blob_init)))
    return m;
  JS_AddModuleExport(ctx, m, "Blob");
  if(!strcmp(module_name, "blob"))
    JS_AddModuleExport(ctx, m, "default");
  return m;
}