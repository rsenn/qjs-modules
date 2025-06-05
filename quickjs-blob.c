#include "defines.h"
#include "quickjs-blob.h"
#include "utils.h"
#include "buffer-utils.h"
#include "iteration.h"
#include "debug.h"

/**
 * \addtogroup quickjs-blob
 * @{
 */

JSClassID js_blob_class_id = 0;
static JSValue blob_proto, blob_ctor;

static Blob*
blob_new(JSContext* ctx, const char* type) {
  Blob* blob;

  if(!(blob = js_mallocz(ctx, sizeof(Blob))))
    return 0;

  blob->type = type ? js_strdup(ctx, type) : 0;

  vector_init(&blob->vec, ctx);

  return blob;
}

static void
blob_free(JSRuntime* rt, Blob* blob) {
  vector_free(&blob->vec);

  js_free_rt(rt, blob);
}

static inline ssize_t
blob_write(JSContext* ctx, Blob* blob, const void* x, size_t len) {
  if(dbuf_put(&blob->vec, x, len))
    return -1;

  return len;
}

static inline InputBuffer
blob_input(JSContext* ctx, Blob* blob) {
  return (InputBuffer){
      {{blob->data, blob->size}},
      0,
      &input_buffer_free_default,
      JS_UNDEFINED,
      OFFSETLENGTH_INIT(),
  };
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

  if(!(blob = blob_new(ctx, type)))
    return JS_EXCEPTION;

  if(x && len) {
    if(dbuf_put(&blob->vec, x, len)) {
      blob_free(JS_GetRuntime(ctx), blob);
      return JS_EXCEPTION;
    }
  }

  return js_blob_wrap(ctx, blob);
}

enum {
  BLOB_SIZE,
  BLOB_TYPE,
};

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
  JSValue proto, obj = JS_UNDEFINED;
  Blob* blob;

  if(!(blob = js_mallocz(ctx, sizeof(Blob))))
    return JS_EXCEPTION;

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_blob_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  blob->type = 0;

  if(argc >= 1) {
    Iteration iter = ITERATION_INIT();

    if(iteration_method_symbol(&iter, ctx, argv[0], "iterator")) {
      int i = 0;

      while(!iteration_next(&iter, ctx)) {
        Blob* other;
        JSValue value = iteration_value(&iter, ctx);
        InputBuffer input = (other = js_blob_data(ctx, value)) ? blob_input(ctx, other) : js_input_chars(ctx, value);
        JS_FreeValue(ctx, value);

        ++i;

        if(input.data == 0) {
          JS_ThrowTypeError(ctx, "item #%d supplied is not <Blob | ArrayBuffer | TypedArray | String>", i);
          goto fail;
        }

        if(blob_write(ctx, blob, input_buffer_data(&input), input_buffer_length(&input)) == -1) {
          JS_ThrowInternalError(ctx, "blob_write returned -1");
          goto fail;
        }

        input_buffer_free(&input, ctx);
      }
    }

    iteration_reset(&iter, ctx);
  }

  if(argc >= 2 && JS_IsObject(argv[1])) {
    if(js_has_propertystr(ctx, argv[1], "type"))
      blob->type = js_get_propertystr_string(ctx, argv[1], "type");
  }

  if(blob->type == 0)
    blob->type = js_strdup(ctx, "application/binary");

  JS_SetOpaque(obj, blob);
  return obj;

fail:
  blob_free(JS_GetRuntime(ctx), blob);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  BLOB_ARRAYBUFFER,
  BLOB_SLICE,
  BLOB_STREAM,
  BLOB_TEXT,
};

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

        s = WRAP_NUM(s, n);
        s = CLAMP_NUM(s, 0, n);

        if(argc >= 2) {
          JS_ToInt64(ctx, &e, argv[1]);

          e = WRAP_NUM(e, n);
          e = CLAMP_NUM(e, 0, n);

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
  JS_NewClassID(&js_blob_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_blob_class_id, &js_blob_class);

  blob_ctor = JS_NewCFunction2(ctx, js_blob_constructor, "Blob", 1, JS_CFUNC_constructor, 0);
  blob_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, blob_proto, js_blob_funcs, countof(js_blob_funcs));

  JS_SetClassProto(ctx, js_blob_class_id, blob_proto);
  JS_SetConstructor(ctx, blob_ctor, blob_proto);

  if(m)
    JS_SetModuleExport(ctx, m, "Blob", blob_ctor);

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

  if((m = JS_NewCModule(ctx, module_name, js_blob_init)))
    JS_AddModuleExport(ctx, m, "Blob");

  return m;
}

/**
 * @}
 */
