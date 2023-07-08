#include "defines.h"
#include "quickjs-blob.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"

/**
 * \addtogroup quickjs-blob
 * @{
 */

JSClassID js_blob_class_id = 0;
JSValue blob_proto = {{0}, JS_TAG_UNDEFINED}, blob_ctor = {{0}, JS_TAG_UNDEFINED};

enum {
  BLOB_SIZE,
  BLOB_TYPE,
};
enum {
  BLOB_ARRAYBUFFER,
  BLOB_SLICE,
  BLOB_STREAM,
  BLOB_TEXT,
};

void
blob_init(JSContext* ctx, Blob* blob, const void* x, size_t len, const char* type) {
  // blob->vec = VECTOR(ctx);
  blob->type = type ? js_strdup(ctx, type) : 0;

  js_dbuf_allocator(ctx, &blob->vec.dbuf);

  if(x && len)
    blob_write(ctx, blob, x, len);
}

Blob*
blob_new(JSContext* ctx, const void* x, size_t len, const char* type) {
  Blob* blob;

  if(!(blob = js_mallocz(ctx, sizeof(Blob))))
    return 0;

  blob_init(ctx, blob, x, len, type);

  return blob;
}

ssize_t
blob_write(JSContext* ctx, Blob* blob, const void* x, size_t len) {
  if(dbuf_put(&blob->vec.dbuf, x, len))
    return -1;

  return len;
}

void
blob_free(JSRuntime* rt, Blob* blob) {
  if(blob->vec.data)
    dbuf_free(&blob->vec.dbuf);

  js_free_rt(rt, blob);
}

InputBuffer
blob_input(JSContext* ctx, Blob* blob) {
  InputBuffer ret = {{{blob->data, blob->size}}, 0, &input_buffer_free_default, JS_UNDEFINED, {0, INT64_MAX}};

  return ret;
}

static void
js_blob_free_func(JSRuntime* rt, void* opaque, void* ptr) {
  // js_free_rt(rt, ptr);
}

JSValue
js_blob_wrap(JSContext* ctx, Blob* blob) {
  JSValue obj = JS_NewObjectProtoClass(ctx, blob_proto, js_blob_class_id);
  JS_SetOpaque(obj, blob);
  return obj;
}

JSValue
js_blob_new(JSContext* ctx, const void* x, size_t len, const char* type) {
  Blob* blob;

  if(!(blob = blob_new(ctx, x, len, type)))
    return JS_EXCEPTION;

  return js_blob_wrap(ctx, blob);
}

static JSValue
js_blob_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Blob* blob;
  JSValue ret = JS_UNDEFINED;

  if(!(blob = js_blob_data(ctx, this_val)))
    return JS_EXCEPTION;

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

static JSValue
js_blob_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue obj = JS_UNDEFINED;
  JSValue proto;
  Blob* blob;

  if(!(blob = blob_new(ctx, 0, 0, 0)))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  /*if(!JS_IsObject(proto))
    proto = blob_proto;*/

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_blob_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  size_t size = 0;
  blob->type = 0;

  if(argc >= 1) {

    // XXX:
    // if(js_is_iterable(ctx, argv[0])) {}

    if(js_is_array(ctx, argv[0])) {
      uint32_t i, len = js_array_length(ctx, argv[0]);
      InputBuffer* parts = js_malloc(ctx, sizeof(InputBuffer) * len);

      for(i = 0; i < len; i++) {
        Blob* other;
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        parts[i] = (other = js_blob_data(ctx, item)) ? blob_input(ctx, other) : js_input_chars(ctx, item);
        size += parts[i].size;
        JS_FreeValue(ctx, item);
      }

      for(i = 0; i < len; i++) {
        if(blob_write(ctx, blob, input_buffer_data(&parts[i]), input_buffer_length(&parts[i])) == -1) {

          while(i < len)
            input_buffer_free(&parts[i++], ctx);

          blob_free(JS_GetRuntime(ctx), blob);
          js_free(ctx, parts);
          return JS_ThrowInternalError(ctx, "blob_write returned -1");
        }

        input_buffer_free(&parts[i], ctx);
      }

      js_free(ctx, parts);

    } else {
      JS_ThrowInternalError(ctx, "argument 1 must be array");
      goto fail;
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

    case BLOB_SLICE: {
      int64_t n = blob->size, s = 0, e = INT64_MAX;
      char* type = 0;

      if(argc >= 1) {
        JS_ToInt64(ctx, &s, argv[0]);
        if(s < 0)
          s = n + s % n;
        else if(s > n)
          s = n;

        if(argc >= 2) {
          JS_ToInt64(ctx, &e, argv[1]);
          if(e < 0)
            e = n + e % n;
          else if(e > n)
            e = n;

          if(argc >= 3)
            type = js_tostring(ctx, argv[2]);
        }
      }

      ret = js_blob_new(ctx, &blob->data[s], e - s, type ? type : blob->type);

      if(type)
        js_free(ctx, type);

      break;
    }

    case BLOB_STREAM: {
      ret = JS_UNDEFINED;
      break;
    }

    case BLOB_TEXT: {
      ret = JS_NewStringLen(ctx, (const char*)blob->data, blob->size);
      break;
    }
  }

  return ret;
}

static JSValue
js_blob_inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Blob* blob;

  if(!(blob = js_blob_data(ctx, this_val)))
    return JS_EXCEPTION;

  JSValue obj = JS_NewObjectClass(ctx, js_blob_class_id);

  JS_DefinePropertyValueStr(ctx, obj, "size", JS_NewUint32(ctx, blob->size), JS_PROP_ENUMERABLE);
  JS_DefinePropertyValueStr(ctx, obj, "type", JS_NewString(ctx, blob->type), JS_PROP_ENUMERABLE);

  return obj;
}

static void
js_blob_finalizer(JSRuntime* rt, JSValue val) {
  Blob* blob;

  if((blob = JS_GetOpaque(val, js_blob_class_id)))
    blob_free(rt, blob);
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

  assert(js_blob_class_id == 0);

  JS_NewClassID(&js_blob_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_blob_class_id, &js_blob_class);

  blob_ctor = JS_NewCFunction2(ctx, js_blob_constructor, "Blob", 1, JS_CFUNC_constructor, 0);
  blob_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, blob_proto, js_blob_funcs, countof(js_blob_funcs));

  JS_SetClassProto(ctx, js_blob_class_id, blob_proto);
  JS_SetConstructor(ctx, blob_ctor, blob_proto);

  js_set_inspect_method(ctx, blob_proto, js_blob_inspect);

  if(m) {
    JS_SetModuleExport(ctx, m, "Blob", blob_ctor);
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

  if((m = JS_NewCModule(ctx, module_name, js_blob_init))) {
    JS_AddModuleExport(ctx, m, "Blob");
  }

  return m;
}

/**
 * @}
 */
