#include "defines.h"
#include "utils.h"
#include "buffer-utils.h"
#include "debug.h"
#include "queue.h"

/**
 * \defgroup quickjs-stream QuickJS module: stream - Buffered stream
 * @{
 */
 
thread_local VISIBLE JSClassID js_stream_class_id = 0;
thread_local JSValue stream_proto = {{JS_TAG_UNDEFINED}}, stream_ctor = {{JS_TAG_UNDEFINED}};

enum stream_functions {
  STATIC_RACE = 0,
  STATIC_MERGE,
  STATIC_ZIP,
};
enum stream_getters {
  PROP_LENGTH = 0,
  PROP_PATH,
};

enum stream_state { STREAM_INITIAL = -1, STREAM_READY = 0, STREAM_CLOSED = 1, STREAM_LOCKED = 2 };

struct stream_item {
  struct list_head link;
  MemoryBlock data;
};

typedef struct stream_object {
  int ref_count;
  BOOL binary : 1;
  enum stream_state state : 2;
  Queue q;
} Stream;

static void
stream_chunk_free(JSRuntime* rt, void* opaque, void* ptr) {
  Chunk* chunk = opaque;

  chunk_free(chunk);
}

static JSValue
stream_chunk_arraybuf(Chunk* chunk, JSContext* ctx) {
  JSValue ret;
  ret = JS_NewArrayBuffer(ctx, chunk->data, chunk->size, stream_chunk_free, chunk, FALSE);
  return ret;
}

static Stream*
stream_new(JSContext* ctx) {
  Stream* strm;

  if((strm = js_mallocz(ctx, sizeof(Stream)))) {
    strm->ref_count = 1;
    queue_init(&strm->q);
  }

  return strm;
}

static void
stream_decrement_refcount(void* opaque) {
  Stream* strm = opaque;

  --strm->ref_count;
}

static JSValue
stream_next(Stream* strm, JSContext* ctx) {
  JSValue ret = JS_UNDEFINED;
  Chunk* chunk;

  if((chunk = queue_next(&strm->q))) {
    ret = stream_chunk_arraybuf(chunk, ctx);
  }

  return ret;
}

static JSValue
js_stream_write(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  Stream* strm;
  MemoryBlock b;
  OffsetLength o;
  InputBuffer input;
  ssize_t ret;

  if(!(strm = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  input = js_input_chars(ctx, argv[0]);
  js_offset_length(ctx, input.size, argc - 1, argv + 1, &o);
  b = input_buffer_block(&input);
  b = block_range(&b, &o);

  ret = queue_write(&strm->q, b.base, b.size);

  if(ret < 0)
    return JS_ThrowInternalError(ctx, "Error writing %zu bytes to queue", b.size);

  return JS_NewInt64(ctx, ret);
}

static JSValue
js_stream_read(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Stream* strm;
  MemoryBlock b;
  OffsetLength o;
  InputBuffer input;
  ssize_t ret;

  if(!(strm = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  input = js_input_chars(ctx, argv[0]);
  js_offset_length(ctx, input.size, argc - 1, argv + 1, &o);
  b = input_buffer_block(&input);
  b = block_range(&b, &o);

  ret = magic ? queue_peek(&strm->q, b.base, b.size) : queue_read(&strm->q, b.base, b.size);

  return JS_NewInt64(ctx, ret);
}

JSValue
js_stream_new(JSContext* ctx, JSValueConst proto) {
  Stream* strm;
  JSValue obj = JS_UNDEFINED;

  if(!(strm = stream_new(ctx)))
    return JS_EXCEPTION;

  obj = JS_NewObjectProtoClass(ctx, proto, js_stream_class_id);
  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, strm);

  return obj;
fail:
  js_free(ctx, strm);
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

JSValue
js_stream_wrap(JSContext* ctx, Stream* strm) {
  JSValue obj;
  obj = JS_NewObjectProtoClass(ctx, stream_proto, js_stream_class_id);
  JS_SetOpaque(obj, strm);
  return obj;
}

static JSValue
js_stream_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto;
  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    proto = JS_DupValue(ctx, stream_proto);

  return js_stream_new(ctx, proto);
}

static JSValue
js_stream_next(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], BOOL* pdone, int magic) {
  Stream* strm;
  JSValue ret = JS_UNDEFINED;

  if(!(strm = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  *pdone = queue_empty(&strm->q);

  if(!*pdone)
    ret = stream_next(strm, ctx);

  return ret;
}

static JSValue
js_stream_iterator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  return JS_DupValue(ctx, this_val);
}

enum {
  FUNC_CLEAR = 0,
};

static JSValue
js_stream_funcs(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  Stream* strm;
  JSValue ret = JS_UNDEFINED;

  if(!(strm = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case FUNC_CLEAR: {
      queue_clear(&strm->q);
      break;
    }
  }
  return ret;
}

enum { PROP_READY, PROP_CLOSED, PROP_LOCKED, PROP_STATE, PROP_SIZE, PROP_EMPTY };

static JSValue
js_stream_get(JSContext* ctx, JSValueConst this_val, int magic) {
  Stream* strm;
  JSValue ret = JS_UNDEFINED;

  if(!(strm = JS_GetOpaque2(ctx, this_val, js_stream_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_READY: ret = JS_NewBool(ctx, strm->state == STREAM_READY); break;
    case PROP_CLOSED: ret = JS_NewBool(ctx, strm->state & STREAM_CLOSED); break;
    case PROP_LOCKED: ret = JS_NewBool(ctx, strm->state & STREAM_LOCKED); break;
    case PROP_STATE: ret = JS_NewInt32(ctx, strm->state); break;
    case PROP_SIZE: ret = JS_NewInt64(ctx, queue_size(&strm->q)); break;
    case PROP_EMPTY: ret = JS_NewBool(ctx, queue_size(&strm->q) == 0); break;
  }
  return ret;
}

static void
stream_finalizer(JSRuntime* rt, Stream* strm) {
  if(--strm->ref_count == 0) {
    queue_clear(&strm->q);

    js_free_rt(rt, strm);
  }
}

static void
js_stream_finalizer(JSRuntime* rt, JSValue val) {
  Stream* strm;

  if((strm = JS_GetOpaque(val, js_stream_class_id)))
    stream_finalizer(rt, strm);

  // JS_FreeValueRT(rt, val);
}

static JSClassDef js_stream_class = {
    .class_name = "Stream",
    .finalizer = js_stream_finalizer,
};

static const JSCFunctionListEntry js_stream_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_stream_next, 0),
    JS_CFUNC_DEF("write", 1, js_stream_write),
    JS_CFUNC_MAGIC_DEF("read", 1, js_stream_read, 0),
    JS_CFUNC_MAGIC_DEF("peek", 1, js_stream_read, 1),
    JS_CFUNC_MAGIC_DEF("clear", 0, js_stream_funcs, FUNC_CLEAR),
    JS_CGETSET_MAGIC_FLAGS_DEF("ready", js_stream_get, 0, PROP_READY, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("closed", js_stream_get, 0, PROP_CLOSED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("locked", js_stream_get, 0, PROP_LOCKED, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("state", js_stream_get, 0, PROP_STATE, JS_PROP_CONFIGURABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("empty", js_stream_get, 0, PROP_EMPTY, JS_PROP_CONFIGURABLE),
    JS_CGETSET_MAGIC_FLAGS_DEF("size", js_stream_get, 0, PROP_SIZE, JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Stream", JS_PROP_C_W_E),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_stream_iterator),
};

static const JSCFunctionListEntry js_stream_static_funcs[] = {
    JS_PROP_INT32_DEF("READY", STREAM_READY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("CLOSED", STREAM_CLOSED, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("LOCKED", STREAM_LOCKED, JS_PROP_ENUMERABLE),
};

static int
js_stream_init(JSContext* ctx, JSModuleDef* m) {

  JS_NewClassID(&js_stream_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_stream_class_id, &js_stream_class);

  stream_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, stream_proto, js_stream_proto_funcs, countof(js_stream_proto_funcs));
  JS_SetClassProto(ctx, js_stream_class_id, stream_proto);

  stream_ctor = JS_NewCFunction2(ctx, js_stream_constructor, "Stream", 1, JS_CFUNC_constructor, 0);

  JS_SetConstructor(ctx, stream_ctor, stream_proto);
  JS_SetPropertyFunctionList(ctx, stream_ctor, js_stream_static_funcs, countof(js_stream_static_funcs));

  if(m) {
    JS_SetModuleExport(ctx, m, "Stream", stream_ctor);
  }

  return 0;
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_stream
#endif

VISIBLE JSModuleDef*
JS_INIT_MODULE(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;
  m = JS_NewCModule(ctx, module_name, &js_stream_init);
  if(!m)
    return NULL;
  JS_AddModuleExport(ctx, m, "Stream");
  return m;
}

/**
 * @}
 */
