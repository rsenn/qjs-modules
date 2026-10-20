#include "defines.h"
#include "quickjs-blob.h"
#include "utils.h"
#include "js-utils.h"
#include "buffer-utils.h"
#include "stream-utils.h"
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
  return INPUTBUFFER_DATA(blob->data, blob->size);
}

static void
blob_stream_finalizer(JSRuntime* rt, void* opaque) {
  InputBuffer* input = opaque;

  if(input) {
    if(input->data)
      js_free_rt(rt, input->data);

    js_free_rt(rt, input);
  }
}

typedef struct {
  Reader reader;
  size_t chunk_size;
} ReaderSource;

static void
reader_source_finalizer(JSRuntime* rt, void* opaque) {
  ReaderSource* rs = opaque;

  reader_free(&rs->reader);
  js_free_rt(rt, rs);
}

static void
reader_source_buffer_free(JSRuntime* rt, void* opaque, void* ptr) {
  js_free_rt(rt, ptr);
}

/* pull(controller): reads up to chunk_size bytes into a new Uint8Array and
 * enqueues it, closes the stream at the end of the reader.
 * returns what enqueue() or close() returns, or JS_EXCEPTION. */
static JSValue
reader_source_pull(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  ReaderSource* rs = opaque;
  JSValueConst controller = argc > 0 ? argv[0] : JS_UNDEFINED;
  JSValue fn, ret, chunk, buffer;
  uint8_t* buf;
  ssize_t n;

  if(!(buf = js_malloc(ctx, rs->chunk_size)))
    return JS_EXCEPTION;

  if((n = reader_read(&rs->reader, buf, rs->chunk_size)) <= 0) {
    js_free(ctx, buf);

    if(n < 0)
      return JS_ThrowInternalError(ctx, "Blob.stream(): read error");

    fn = JS_GetPropertyStr(ctx, controller, "close");
    ret = JS_Call(ctx, fn, controller, 0, 0);
    JS_FreeValue(ctx, fn);
    return ret;
  }

  buffer = JS_NewArrayBuffer(ctx, buf, n, reader_source_buffer_free, NULL, FALSE);
  chunk = js_typedarray_new(ctx, 8, FALSE, FALSE, buffer);
  JS_FreeValue(ctx, buffer);

  fn = JS_GetPropertyStr(ctx, controller, "enqueue");
  ret = JS_Call(ctx, fn, controller, 1, &chunk);
  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, chunk);
  return ret;
}

/**
 * a ReadableStream over a Reader, chunk_size bytes per pull().
 *
 * ReadableStream comes from the stream module, the compiled lib/stream.js
 * in qjsm, so the stream has pipeTo(), pipeThrough() and the rest.
 *
 *   JSContext*  ctx         the context
 *   Reader      reader      the source, owned by the stream from here on
 *   size_t      chunk_size  bytes per pull(), 0 for 64 KiB
 *
 *   returns JSValue         the stream, or JS_EXCEPTION with an exception
 *                           pending (reader is freed then)
 */
static JSValue
js_readable_stream_from_reader(JSContext* ctx, Reader reader, size_t chunk_size) {
  JSValue ns, ctor, source, stream;
  ReaderSource* rs;

  if(!JS_IsObject(ns = js_module_namespace_sync(ctx, "stream"))) {
    reader_free(&reader);
    JS_FreeValue(ctx, ns);
    return JS_ThrowReferenceError(ctx, "Blob.stream(): 'stream' module required");
  }

  ctor = JS_GetPropertyStr(ctx, ns, "ReadableStream");
  JS_FreeValue(ctx, ns);

  if(!JS_IsConstructor(ctx, ctor)) {
    reader_free(&reader);
    JS_FreeValue(ctx, ctor);
    return JS_ThrowReferenceError(ctx, "Blob.stream(): no ReadableStream in the 'stream' module");
  }

  if(!(rs = js_mallocz(ctx, sizeof(ReaderSource)))) {
    reader_free(&reader);
    JS_FreeValue(ctx, ctor);
    return JS_EXCEPTION;
  }

  rs->reader = reader;
  rs->chunk_size = chunk_size ? chunk_size : 65536;

  source = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, source, "pull", js_function_cclosure(ctx, reader_source_pull, 1, 0, rs, reader_source_finalizer));

  stream = JS_CallConstructor(ctx, ctor, 1, (JSValueConst*)&source);
  JS_FreeValue(ctx, source);
  JS_FreeValue(ctx, ctor);
  return stream;
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

  if(!(blob = JS_GetOpaque2(ctx, this_val, js_blob_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case BLOB_SIZE: {
      ret = JS_NewUint32(ctx, blob->size);
      break;
    }
    case BLOB_TYPE: {
      ret = JS_NewString(ctx, blob->type ? blob->type : "");
      break;
    }
  }

  return ret;
}

static JSValue
js_blob_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  Blob* blob;

  if(!(blob = blob_new(ctx, 0)))
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

  if(argc >= 1) {
    if(!JS_IsObject(argv[0]) || JS_IsNull(argv[0])) {
      JS_ThrowTypeError(ctx, "argument 1 must be an object");
      goto fail;
    }

    Iteration iter = ITERATION_INIT();

    if(!iteration_method_symbol(&iter, ctx, argv[0], "iterator")) {
      JS_ThrowTypeError(ctx, "argument 1 must be an iterable");
      goto fail;
    }
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

      if(blob_write(ctx, blob, inputbuffer_data(&input), inputbuffer_length(&input)) == -1) {
        JS_ThrowInternalError(ctx, "blob_write returned -1");
        goto fail;
      }

      inputbuffer_free(&input, ctx);
    }

    iteration_reset(&iter, ctx);
  }

  if(argc > 1 && !js_is_null_or_undefined(argv[1])) {

    if(!JS_IsObject(argv[1])) {
      JS_ThrowTypeError(ctx, "argument 2 must be an object");
      goto fail;
    }

    if(js_has_propertystr(ctx, argv[1], "type")) {
      char* type;

      if((type = js_get_propertystr_string(ctx, argv[1], "type"))) {
        size_t i = 0;

        for(i = 0; type[i]; i++) {
          if(type[i] < 0x20 || type[i] >= 0x7f) {
            js_free(ctx, type);
            type = 0;
            break;
          }
        }

        blob->type = type;
      }
    }
  }

  JS_SetOpaque(obj, blob);
  return obj;

fail:
  blob_free(JS_GetRuntime(ctx), blob);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  BLOB_ARRAYBUFFER,
  BLOB_BYTES,
  BLOB_TEXT,
  BLOB_JSON,
  BLOB_SLICE,
  BLOB_STREAM,
};

static JSValue
js_blob_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Blob* blob;
  JSValue ret = JS_UNDEFINED;

  if(!(blob = JS_GetOpaque2(ctx, this_val, js_blob_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case BLOB_ARRAYBUFFER: {
      ret = promise_immediate(ctx, FALSE, js_arraybuffer_fromvalue(ctx, blob->data, blob->size, this_val));
      break;
    }
    case BLOB_BYTES: {
      JSValue buf = js_arraybuffer_fromvalue(ctx, blob->data, blob->size, this_val);

      ret = promise_immediate(ctx, FALSE, js_typedarray_new(ctx, 8, FALSE, FALSE, buf));

      JS_FreeValue(ctx, buf);
      break;
    }
    case BLOB_TEXT: {
      ret = promise_immediate(ctx, FALSE, JS_NewStringLen(ctx, (const char*)blob->data, blob->size));
      break;
    }
    case BLOB_JSON: {
      /* json(): text() then JSON.parse; a bad document rejects, never throws.
       * the BOM is dropped; invalid UTF-8 decodes to U+FFFD like text(). */
      const uint8_t* data = blob->data;
      size_t size = blob->size;
      JSValue str, parsed = JS_EXCEPTION;
      const char* cstr;
      size_t len;

      if(size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
        data += 3;
        size -= 3;
      }

      if(JS_IsException(str = JS_NewStringLen(ctx, (const char*)data, size)))
        return JS_EXCEPTION;

      /* JS_ParseJSON wants NUL-terminated input; JS_ToCStringLen provides it */
      if((cstr = JS_ToCStringLen(ctx, &len, str))) {
        parsed = JS_ParseJSON(ctx, cstr, len, "<blob>");
        JS_FreeCString(ctx, cstr);
      }

      JS_FreeValue(ctx, str);

      if(JS_IsException(parsed)) {
        JSValue error = JS_GetException(ctx);

        ret = promise_immediate(ctx, TRUE, error);
        JS_FreeValue(ctx, error);
      } else {
        ret = promise_immediate(ctx, FALSE, parsed);
        JS_FreeValue(ctx, parsed);
      }
      break;
    }
    case BLOB_SLICE: {
      IndexRange rng = INDEX_RANGE_INIT();
      char* type = 0;
      int index = js_index_range(ctx, blob->size, argc, argv, 0, &rng);

      if(argc > index)
        type = js_tostring(ctx, argv[index]);

      ret = js_blob_new(ctx, indexrange_begin(rng, blob->data, blob->size), indexrange_size(rng, blob->size), type ? type : blob->type);

      if(type)
        js_free(ctx, type);

      break;
    }
    case BLOB_STREAM: {
      InputBuffer* input;
      uint8_t* data_copy;

      /* Create a ReadableStream over the blob's data.
       * We need to copy the blob data because the blob object may be
       * garbage collected before the stream finishes reading. We also
       * allocate the InputBuffer on the heap so the Reader can own it. */
      if(!(data_copy = js_malloc(ctx, blob->size))) {
        ret = JS_EXCEPTION;
        break;
      }
      memcpy(data_copy, blob->data, blob->size);

      if(!(input = js_mallocz(ctx, sizeof(InputBuffer)))) {
        js_free(ctx, data_copy);
        ret = JS_EXCEPTION;
        break;
      }
      *input = INPUTBUFFER_DATA(data_copy, blob->size);

      /* Create a Reader that will free both the InputBuffer and the data copy */
      Reader reader = (Reader){
          (ReadFunction*)&inputbuffer_read,
          input,
          NULL,
          (ReaderFinalizer*)blob_stream_finalizer,
      };

      ret = js_readable_stream_from_reader(ctx, reader, 0);
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
    JS_CFUNC_MAGIC_DEF("bytes", 0, js_blob_method, BLOB_BYTES),
    JS_CFUNC_MAGIC_DEF("text", 0, js_blob_method, BLOB_TEXT),
    JS_CFUNC_MAGIC_DEF("json", 0, js_blob_method, BLOB_JSON),
    JS_CFUNC_MAGIC_DEF("stream", 0, js_blob_method, BLOB_STREAM),
    JS_CFUNC_MAGIC_DEF("slice", 0, js_blob_method, BLOB_SLICE),
    JS_CGETSET_MAGIC_FLAGS_DEF("size", js_blob_get, 0, BLOB_SIZE, JS_PROP_ENUMERABLE),
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

#ifdef JS_SHARED_LIBRARY
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
