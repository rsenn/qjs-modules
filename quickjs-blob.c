/* quickjs-blob.c: Blob, arrayBuffer(), bytes(), text(), json(), slice(),
 * stream().
 * depends on: buffer-utils.c (InputBuffer segments), vector.c,
 * stream-utils.c (Reader), iteration.c, js-utils.c (promises).
 * rule: segment bytes never change, a buffer or string part is copied
 * once, a Blob part or a slice shares the segments of its source. */
#include "defines.h"
#include "quickjs-blob.h"
#include "quickjs.h"
#include "utils.h"
#include "js-utils.h"
#include "buffer-utils.h"
#include "stream-utils.h"
#include "iteration.h"
#include "debug.h"
#include "json/jevent-js.h"

/**
 * \addtogroup quickjs-blob
 * @{
 */

JSClassID js_blob_class_id = 0;
static JSValue blob_proto, blob_ctor;

/* InputBuffer free callback of an owning segment: frees the copy */
static void
segment_free_owned(JSContext* ctx, JSValue value, InputBuffer* seg) {
  js_free(ctx, seg->data);
}

/* InputBuffer free callback of a non-owning segment: drops the ref */
static void
segment_free_ref(JSContext* ctx, JSValue value, InputBuffer* seg) {
  JS_FreeValue(ctx, value);
}

static Blob*
blob_new(JSContext* ctx, const char* type) {
  Blob* blob;

  if(!(blob = js_mallocz(ctx, sizeof(Blob))))
    return 0;

  blob->type = type ? js_strdup(ctx, type) : 0;
  vector_init_rt(&blob->segments, JS_GetRuntime(ctx));
  return blob;
}

/* releases the segments and the type, not blob itself. never throws. */
static void
blob_clear(JSRuntime* rt, Blob* blob) {
  InputBuffer* seg;

  vector_foreach_t(&blob->segments, seg) {
    if(JS_IsUndefined(seg->value))
      js_free_rt(rt, seg->data);
    else
      JS_FreeValueRT(rt, seg->value);
  }

  vector_free(&blob->segments);

  if(blob->type)
    js_free_rt(rt, blob->type);
}

static void
blob_free(JSRuntime* rt, Blob* blob) {
  blob_clear(rt, blob);
  js_free_rt(rt, blob);
}

/**
 * appends a segment to the list and adds its length to the size.
 *
 *   Blob*         blob  the Blob to append to
 *   InputBuffer*  seg   taken over, freed on failure too
 *
 *   returns int         0, or -1 with an exception pending
 */
static int
blob_add(JSContext* ctx, Blob* blob, InputBuffer* seg) {
  if(!vector_push(&blob->segments, *seg)) {
    inputbuffer_free(seg, ctx);
    JS_ThrowOutOfMemory(ctx);
    return -1;
  }

  blob->size += inputbuffer_length(seg);
  return 0;
}

/**
 * appends a copy of the bytes as an owning segment.
 *
 *   Blob*        blob  the Blob to append to
 *   const void*  x     the bytes
 *   size_t       len   their number, 0 appends nothing
 *
 *   returns int        0, or -1 with an exception pending
 */
static int
blob_copy(JSContext* ctx, Blob* blob, const void* x, size_t len) {
  InputBuffer seg;
  uint8_t* data;

  if(!len)
    return 0;

  if(!(data = js_malloc(ctx, len)))
    return -1;

  memcpy(data, x, len);
  seg = INPUTBUFFER_DATA_FREE(data, len, &segment_free_owned);
  return blob_add(ctx, blob, &seg);
}

/**
 * appends the bytes offset to offset + length of src, without copying.
 *
 * each new segment is non-owning: it refers to owner (the JS object of
 * src) for an owning segment of src, else to what that segment refers to,
 * so a slice of a slice still refers to the Blob that owns the bytes.
 *
 *   Blob*         blob    the Blob to append to
 *   JSValueConst  owner   the JS object whose opaque is src
 *   Blob*         src     the Blob to share segments of
 *   size_t        offset  first byte of src
 *   size_t        length  number of bytes
 *
 *   returns int           0, or -1 with an exception pending
 */
static int
blob_share(JSContext* ctx, Blob* blob, JSValueConst owner, Blob* src, size_t offset, size_t length) {
  InputBuffer* seg;

  vector_foreach_t(&src->segments, seg) {
    size_t len = inputbuffer_length(seg);
    InputBuffer part;

    if(!length)
      break;

    if(offset >= len) {
      offset -= len;
      continue;
    }

    part = INPUTBUFFER_DATA_FREE(seg->data, seg->size, &segment_free_ref);
    part.range = OFFSET_LENGTH(seg->range.offset + offset, MIN_NUM(len - offset, length));
    part.value = JS_DupValue(ctx, JS_IsUndefined(seg->value) ? owner : seg->value);
    offset = 0;
    length -= part.range.length;

    if(blob_add(ctx, blob, &part))
      return -1;
  }

  return 0;
}

/* a new ArrayBuffer with a copy of all bytes, its own memory.
 * returns JS_EXCEPTION with an exception pending on failure. */
static JSValue
blob_arraybuffer(JSContext* ctx, Blob* blob) {
  DynBuf db = DBUF_INIT_RT(JS_GetRuntime(ctx));
  InputBuffer* seg;

  if(!blob->size)
    return JS_NewArrayBufferCopy(ctx, (const uint8_t*)"", 0);

  if(dbuf_claim(&db, blob->size))
    return JS_ThrowOutOfMemory(ctx);

  vector_foreach_t(&blob->segments, seg) dbuf_put(&db, inputbuffer_data(seg), inputbuffer_length(seg));

  return dbuf_toarraybuffer(&db, ctx);
}

/**
 * all bytes in one piece, without a copy for a single segment.
 *
 *   Blob*    blob  the Blob to read
 *   DynBuf*  db    holds the copy of several segments, the caller
 *                  frees it with dbuf_free
 *
 *   returns const uint8_t*  the bytes, or NULL with an exception pending
 */
static const uint8_t*
blob_bytes(JSContext* ctx, Blob* blob, DynBuf* db) {
  InputBuffer* seg;

  switch(vector_size(&blob->segments, sizeof(InputBuffer))) {
    case 0: return (const uint8_t*)"";
    case 1: return inputbuffer_data(vector_begin(&blob->segments));
  }

  if(dbuf_claim(db, blob->size)) {
    JS_ThrowOutOfMemory(ctx);
    return 0;
  }

  vector_foreach_t(&blob->segments, seg) dbuf_put(db, inputbuffer_data(seg), inputbuffer_length(seg));

  return db->buf;
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
  uint8_t *buf, *tmp;
  ssize_t n;

  if(!(buf = js_malloc(ctx, rs->chunk_size)))
    return JS_EXCEPTION;

  if((n = reader_read(&rs->reader, buf, rs->chunk_size)) <= 0) {
    js_free(ctx, buf);

    if(n < 0)
      return JS_ThrowInternalError(ctx, "Blob.stream(): read error");

    return js_invoke(ctx, controller, "close", 0, 0);
  }

  /* a short read leaves the rest of buf unused */
  if((size_t)n < rs->chunk_size && (tmp = js_realloc(ctx, buf, n)))
    buf = tmp;

  JSValue buffer = JS_NewArrayBuffer(ctx, buf, n, reader_source_buffer_free, NULL, FALSE);
  JSValue chunk = js_typedarray_new(ctx, 8, FALSE, FALSE, buffer);
  JS_FreeValue(ctx, buffer);

  JSValue ret = js_invoke(ctx, controller, "enqueue", 1, &chunk);
  JS_FreeValue(ctx, chunk);
  return ret;
}

/**
 * an underlying source object { pull } over a Reader, chunk_size bytes
 * per pull().
 *
 *   JSContext*  ctx         the context
 *   Reader      reader      the source, owned by the object from here on
 *   size_t      chunk_size  bytes per pull(), 0 for 64 KiB
 *
 *   returns JSValue         the source object, or JS_EXCEPTION with an
 *                           exception pending (reader is freed then)
 */
static JSValue
reader_source_new(JSContext* ctx, Reader reader, size_t chunk_size) {
  ReaderSource* rs;

  if(!(rs = js_mallocz(ctx, sizeof(ReaderSource)))) {
    reader_free(&reader);
    return JS_EXCEPTION;
  }

  rs->reader = reader;
  rs->chunk_size = chunk_size ? chunk_size : 65536;

  JSValue source = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, source, "pull", js_function_cclosure(ctx, reader_source_pull, 1, 0, rs, reader_source_finalizer));
  return source;
}

/* the Reader of stream(): its own segment list, the index of the
 * segment being read, reader_free() passes the runtime as opaque2. */
typedef struct {
  Blob blob;
  uint32_t index;
} BlobReader;

/* reads from the current segment, never past its end, so every pull()
 * of the stream gives one segment. returns 0 at the end. */
static ssize_t
blob_reader_read(intptr_t opaque, void* buf, size_t len, Reader* rd) {
  BlobReader* br = (BlobReader*)opaque;
  uint32_t n = vector_size(&br->blob.segments, sizeof(InputBuffer));

  for(; br->index < n; br->index++) {
    InputBuffer* seg = vector_at(&br->blob.segments, sizeof(InputBuffer), br->index);

    if(!inputbuffer_eof(seg))
      return inputbuffer_read(seg, buf, len);
  }

  return 0;
}

static void
blob_reader_finalizer(void* opaque, void* rt) {
  BlobReader* br = opaque;

  blob_clear(rt, &br->blob);
  js_free_rt(rt, br);
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

  if(x && blob_copy(ctx, blob, x, len)) {
    blob_free(JS_GetRuntime(ctx), blob);
    return JS_EXCEPTION;
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
      ret = JS_NewInt64(ctx, blob->size);
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
      int r;

      ++i;

      /* a Blob shares its segments, anything else is copied */
      if((other = JS_GetOpaque(value, js_blob_class_id)))
        r = blob_share(ctx, blob, value, other, 0, other->size);
      else {
        InputBuffer input = js_input_chars(ctx, value);

        if(input.data == 0) {
          JS_FreeValue(ctx, value);
          JS_ThrowTypeError(ctx, "item #%d supplied is not <Blob | ArrayBuffer | TypedArray | String>", i);
          goto fail;
        }

        r = blob_copy(ctx, blob, inputbuffer_data(&input), inputbuffer_length(&input));
        inputbuffer_free(&input, ctx);
      }

      JS_FreeValue(ctx, value);

      if(r)
        goto fail;
    }

    iteration_reset(&iter, ctx);
  }

  if(argc > 1 && !js_is_nullish(argv[1])) {
    if(!JS_IsObject(argv[1])) {
      JS_ThrowTypeError(ctx, "argument 2 must be an object");
      goto fail;
    }

    if(js_has_propertystr(ctx, argv[1], "type")) {
      char* type;

      if((type = js_get_propertystr_string(ctx, argv[1], "type"))) {
        for(size_t i = 0; type[i]; i++) {
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
      JSValue buf = blob_arraybuffer(ctx, blob);

      if(JS_IsException(buf))
        return buf;

      ret = promise_immediate(ctx, FALSE, buf);
      JS_FreeValue(ctx, buf);
      break;
    }
    case BLOB_BYTES: {
      JSValue buf = blob_arraybuffer(ctx, blob);

      if(JS_IsException(buf))
        return buf;

      ret = promise_immediate(ctx, FALSE, js_typedarray_new(ctx, 8, FALSE, FALSE, buf));

      JS_FreeValue(ctx, buf);
      break;
    }
    case BLOB_TEXT: {
      DynBuf db = DBUF_INIT_RT(JS_GetRuntime(ctx));
      const uint8_t* data;

      if(!(data = blob_bytes(ctx, blob, &db)))
        return JS_EXCEPTION;

      ret = promise_immediate(ctx, FALSE, JS_NewStringLen(ctx, (const char*)data, blob->size));
      dbuf_free(&db);
      break;
    }
    case BLOB_JSON: {
      /* json(): text() then JSON.parse; a bad document rejects, never throws.
       * the BOM is dropped; invalid UTF-8 decodes to U+FFFD like text().
       * json_read_value does not recurse, so deep nesting works like in Bun. */
      DynBuf db = DBUF_INIT_RT(JS_GetRuntime(ctx));
      const uint8_t* data;
      size_t size = blob->size;
      JSValue str, parsed = JS_EXCEPTION;
      const char* cstr;
      size_t len;

      if(!(data = blob_bytes(ctx, blob, &db)))
        return JS_EXCEPTION;

      if(size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
        data += 3;
        size -= 3;
      }

      str = JS_NewStringLen(ctx, (const char*)data, size);
      dbuf_free(&db);

      if(JS_IsException(str))
        return JS_EXCEPTION;

      /* the string round trip makes the bytes valid UTF-8 */
      if((cstr = JS_ToCStringLen(ctx, &len, str))) {
        parsed = json_read_value(ctx, (const uint8_t*)cstr, len, "<blob>");
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
      int index;
      Blob* slice;

      if((index = js_index_range(ctx, blob->size, argc, argv, 0, &rng)) < 0)
        return JS_EXCEPTION;

      if(argc > index)
        type = js_tostring(ctx, argv[index]);

      slice = blob_new(ctx, type ? type : blob->type);

      if(type)
        js_free(ctx, type);

      if(!slice)
        return JS_EXCEPTION;

      if(blob_share(ctx, slice, this_val, blob, indexrange_head(rng, blob->size), indexrange_size(rng, blob->size))) {
        blob_free(JS_GetRuntime(ctx), slice);
        return JS_EXCEPTION;
      }

      ret = js_blob_wrap(ctx, slice);
      break;
    }
    case BLOB_STREAM: {
      /* the stream reads its own copy of the segment list, which keeps
       * the bytes alive after the Blob is collected */
      JSRuntime* rt = JS_GetRuntime(ctx);
      size_t chunk_size = 0;
      BlobReader* br;
      InputBuffer* seg;

      if(!(br = js_mallocz(ctx, sizeof(BlobReader))))
        return JS_EXCEPTION;

      vector_init_rt(&br->blob.segments, rt);

      if(blob_share(ctx, &br->blob, this_val, blob, 0, blob->size)) {
        blob_reader_finalizer(br, rt);
        return JS_EXCEPTION;
      }

      vector_foreach_t(&br->blob.segments, seg) {
        chunk_size = MAX_NUM(chunk_size, inputbuffer_length(seg));
      }

      ret = js_stream_construct_free(ctx, "ReadableStream", reader_source_new(ctx, (Reader){&blob_reader_read, br, rt, &blob_reader_finalizer}, chunk_size));
      break;
    }
  }

  return ret;
}

/**
 * Blob[Symbol.for('qjs.blob.fromArrayBuffer')]: a Blob over the bytes of
 * an ArrayBuffer without a copy, for fs.openAsBlob() over a mapping.
 *
 *   const blob = Blob[Symbol.for('qjs.blob.fromArrayBuffer')](buffer);
 *
 *   ArrayBuffer  buffer  bytes nobody writes from here on, the Blob
 *                        holds a ref to it
 *
 *   returns Blob         an untyped Blob, new Blob([blob], { type }) types it
 *
 * throws TypeError when buffer is not an ArrayBuffer or is detached.
 * not exported, defined on the Blob constructor in js_blob_init().
 */
static JSValue
js_blob_from_arraybuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Blob* blob;
  uint8_t* data;
  size_t len;

  if(!(data = JS_GetArrayBuffer(ctx, &len, argv[0])))
    return JS_EXCEPTION;

  if(!(blob = blob_new(ctx, 0)))
    return JS_EXCEPTION;

  if(len) {
    InputBuffer seg = INPUTBUFFER_DATA_FREE(data, len, &segment_free_ref);

    seg.value = JS_DupValue(ctx, argv[0]);

    if(blob_add(ctx, blob, &seg)) {
      blob_free(JS_GetRuntime(ctx), blob);
      return JS_EXCEPTION;
    }
  }

  return js_blob_wrap(ctx, blob);
}

/* ArrayBuffer free callback of a segment view: drops the ref on the
 * object that keeps the bytes, opaque is its pointer */
static void
segment_view_free(JSRuntime* rt, void* opaque, void* ptr) {
  JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, opaque));
}

/**
 * blob[Symbol.for('qjs.blob.segments')](): the bytes of the Blob as one
 * Uint8Array per segment, without copying, for fs.writevSync().
 *
 *   writevSync(fd, blob[Symbol.for('qjs.blob.segments')]())
 *
 *   returns Array  Uint8Array views, read only by contract, each one keeps
 *                  the object that owns its bytes alive
 *
 * throws TypeError when this is not a Blob.
 * not exported, defined on Blob.prototype in js_blob_init().
 */
static JSValue
js_blob_segments(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Blob* blob;
  InputBuffer* seg;
  uint32_t i = 0;
  JSValue ret;

  if(!(blob = JS_GetOpaque2(ctx, this_val, js_blob_class_id)))
    return JS_EXCEPTION;

  ret = JS_NewArray(ctx);

  vector_foreach_t(&blob->segments, seg) {
    /* refcount: the ArrayBuffer holds this ref until it is freed */
    JSValue owner = JS_DupValue(ctx, JS_IsUndefined(seg->value) ? this_val : seg->value);
    JSValue buf = JS_NewArrayBuffer(ctx, (uint8_t*)inputbuffer_data(seg), inputbuffer_length(seg), segment_view_free, JS_VALUE_GET_PTR(owner), FALSE);

    if(JS_IsException(buf)) {
      JS_FreeValue(ctx, owner);
      JS_FreeValue(ctx, ret);
      return JS_EXCEPTION;
    }

    JS_SetPropertyUint32(ctx, ret, i++, js_typedarray_new(ctx, 8, FALSE, FALSE, buf));
    JS_FreeValue(ctx, buf);
  }

  return ret;
}

static void
js_blob_finalizer(JSRuntime* rt, JSValue val) {
  Blob* blob;

  if((blob = JS_GetOpaque(val, js_blob_class_id)))
    blob_free(rt, blob);
}

/* marks the values non-owning segments refer to */
static void
js_blob_gc_mark(JSRuntime* rt, JSValueConst val, JS_MarkFunc* mark_func) {
  Blob* blob;
  InputBuffer* seg;

  if((blob = JS_GetOpaque(val, js_blob_class_id))) {
    vector_foreach_t(&blob->segments, seg) {
      JS_MarkValue(rt, seg->value, mark_func);
    }
  }
}

static JSClassDef js_blob_class = {
    .class_name = "Blob",
    .finalizer = js_blob_finalizer,
    .gc_mark = js_blob_gc_mark,
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

  JSAtom from_ab = js_symbol_for_atom(ctx, "qjs.blob.fromArrayBuffer");
  JS_DefinePropertyValue(ctx, blob_ctor, from_ab, JS_NewCFunction(ctx, js_blob_from_arraybuffer, "fromArrayBuffer", 1), JS_PROP_CONFIGURABLE);
  JS_FreeAtom(ctx, from_ab);

  JSAtom segments = js_symbol_for_atom(ctx, "qjs.blob.segments");
  JS_DefinePropertyValue(ctx, blob_proto, segments, JS_NewCFunction(ctx, js_blob_segments, "segments", 0), JS_PROP_CONFIGURABLE);
  JS_FreeAtom(ctx, segments);

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
